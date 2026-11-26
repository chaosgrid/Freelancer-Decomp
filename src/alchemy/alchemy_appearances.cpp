// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_appearances.h"

#include "alchemy_common.h"
#include "alchemy_math.h"
#include "alchemy_ramp.h"
#include "common_misc.h"
#include "render/shading.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// The eight appearance leaves: their property sets (each class's set creator and
// builder, and the set slots' tables), plus the per-leaf update, spawn, bounds and draw
// slots.
namespace AlchemyImpl
{
    namespace
    {

        // Retail FCOMP raises INVALID for quiet NaNs and still treats unordered as unequal.
        // Plain != lowers to quiet FUCOMPP, so preserve ordered relational comparisons.
        // Release /O2 reverses constant/field operands and x87 condition flags without this scope.
#pragma optimize("", off)
#pragma float_control(except, on, push)
        __declspec(noinline) bool IsNonzeroParticleField(const float& Value)
        {
            return Value <= 0.0f ? Value < 0.0f : true;
        }

        __declspec(noinline) bool IsNonunitParticleField(const float& Value)
        {
            return Value <= 1.0f ? Value < 1.0f : true;
        }

        // Basic's unrounded scale uses signalling FCOMP before the zero-only return.
        __declspec(noinline) bool IsZeroBasicScale(const double& Value)
        {
            return Value <= 0.0 ? Value >= 0.0 : false;
        }

        // Perp/Rect frame flips convert only one side through x87; UV words may be sNaN/subnormal.
        __declspec(noinline) double LoadAppearanceScalar(const float& Value)
        {
            return Value;
        }

        // Preserve the cleared DeathEffect at the reciprocal's FP exception point.
        __declspec(noinline) void InitializeParticleReciprocal(FParticleAppRecord& Record)
        {
            Record.DeathEffect = nullptr;
            Record.Base.InvDuration = 1.0f / Record.Base.Duration;
        }

        __declspec(noinline) bool IsMeshParticleExpired(const FPooledParticle& Particle)
        {
            return Particle.Age > Particle.Duration;
        }

        // FUN_06206c40 rounds all three velocity products before position additions.
        __declspec(noinline) void MeshParticleStep(const FPooledParticle& Particle, float Dt, float* Out)
        {
            const double Z = static_cast<double>(Dt) * Particle.VelZ;
            const double Y = static_cast<double>(Dt) * Particle.VelY;
            const double X = static_cast<double>(Dt) * Particle.VelX;
            Out[0] = static_cast<float>(X);
            Out[1] = static_cast<float>(Y);
            Out[2] = static_cast<float>(Z);
        }

        // FUN_06225000 merges zero-radius mesh points; retain each x87 float store.
        __declspec(noinline) void MergeMeshBoundsPoint(float* Sphere, const float* Center)
        {
            const double DifferenceZ = static_cast<double>(Center[2]) - Sphere[2];
            const double DifferenceY = static_cast<double>(Center[1]) - Sphere[1];
            const double DifferenceX = static_cast<double>(Center[0]) - Sphere[0];
            float Difference[3];
            Difference[0] = static_cast<float>(DifferenceX);
            Difference[1] = static_cast<float>(DifferenceY);
            Difference[2] = static_cast<float>(DifferenceZ);
            const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
            const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
            const double SumYz = SquareY + SquareZ;
            const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
            const float LengthSquared = static_cast<float>(SumYz + SquareX);
            std::int32_t LengthBits = 0;
            std::memcpy(&LengthBits, &LengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            const double InverseLength = (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
            const double Distance = InverseLength * LengthSquared;
            const float RoundedDistance = static_cast<float>(Distance);
            // Each test below takes its branch only for an ordered result.
            if (Distance + Sphere[3] < 0.0)
            {
                std::memcpy(Sphere, Center, sizeof(float) * 3);
                Sphere[3] = 0.0f;
                return;
            }
            if (static_cast<double>(RoundedDistance) + 0.0 < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((0.0 + static_cast<double>(Sphere[3])) + RoundedDistance) * 0.5);
            if (!(static_cast<double>(RoundedDistance) <= 1e-6))
            {
                const double InverseDistance = 1.0 / RoundedDistance;
                Difference[0] = static_cast<float>(InverseDistance * Difference[0]);
                Difference[1] = static_cast<float>(InverseDistance * Difference[1]);
                Difference[2] = static_cast<float>(InverseDistance * Difference[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Sphere[3]);
                const double StepZ = static_cast<double>(Step) * Difference[2];
                const double StepY = static_cast<double>(Step) * Difference[1];
                const double StepX = static_cast<double>(Step) * Difference[0];
                float Offset[3];
                Offset[0] = static_cast<float>(StepX);
                Offset[1] = static_cast<float>(StepY);
                Offset[2] = static_cast<float>(StepZ);
                Sphere[0] = static_cast<float>(static_cast<double>(Offset[0]) + Sphere[0]);
                Sphere[1] = static_cast<float>(static_cast<double>(Offset[1]) + Sphere[1]);
                Sphere[2] = static_cast<float>(static_cast<double>(Offset[2]) + Sphere[2]);
            }
            Sphere[3] = NewRadius;
        }
        enum class EPerpRectBoundsDistance
        {
            PerpUnroundedXyz,
            RectRoundedYzx
        };

        // Perp's distance (FUN_06206ad0) keeps the XYZ sum in double; Rect's inline
        // YZX sum is rounded to float.
        __declspec(noinline) void MergePerpRectBoundsSphere(float* Sphere, const float* Center,
            const float& Radius, EPerpRectBoundsDistance DistanceKind)
        {
            const double DifferenceZ = static_cast<double>(Center[2]) - Sphere[2];
            const double DifferenceY = static_cast<double>(Center[1]) - Sphere[1];
            const double DifferenceX = static_cast<double>(Center[0]) - Sphere[0];
            float Difference[3];
            Difference[0] = static_cast<float>(DifferenceX);
            Difference[1] = static_cast<float>(DifferenceY);
            Difference[2] = static_cast<float>(DifferenceZ);
            double LengthSquared;
            float RoundedLengthSquared;
            if (DistanceKind == EPerpRectBoundsDistance::PerpUnroundedXyz)
            {
                const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
                const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
                const double SumXy = SquareX + SquareY;
                const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
                LengthSquared = SumXy + SquareZ;
                RoundedLengthSquared = static_cast<float>(LengthSquared);
            }
            else
            {
                const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
                const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
                const double SumYz = SquareY + SquareZ;
                const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
                RoundedLengthSquared = static_cast<float>(SumYz + SquareX);
                LengthSquared = RoundedLengthSquared;
            }
            std::int32_t LengthBits;
            std::memcpy(&LengthBits, &RoundedLengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            const double InverseLength = (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
            const double Distance = InverseLength * LengthSquared;
            const float RoundedDistance = static_cast<float>(Distance);
            // Each test below takes its branch only for an ordered result.
            if (Distance + Sphere[3] < Radius)
            {
                std::memcpy(Sphere, Center, sizeof(float) * 3);
                std::memcpy(Sphere + 3, &Radius, sizeof(Radius));
                return;
            }
            if (static_cast<double>(Radius) + RoundedDistance < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Radius) + Sphere[3]) + RoundedDistance) * 0.5);
            if (!(static_cast<double>(RoundedDistance) <= 1e-6))
            {
                const double InverseDistance = 1.0 / RoundedDistance;
                Difference[0] = static_cast<float>(InverseDistance * Difference[0]);
                Difference[1] = static_cast<float>(InverseDistance * Difference[1]);
                Difference[2] = static_cast<float>(InverseDistance * Difference[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Sphere[3]);
                const double StepZ = static_cast<double>(Step) * Difference[2];
                const double StepY = static_cast<double>(Step) * Difference[1];
                const double StepX = static_cast<double>(Step) * Difference[0];
                float Offset[3];
                Offset[0] = static_cast<float>(StepX);
                Offset[1] = static_cast<float>(StepY);
                Offset[2] = static_cast<float>(StepZ);
                Sphere[0] = static_cast<float>(static_cast<double>(Offset[0]) + Sphere[0]);
                Sphere[1] = static_cast<float>(static_cast<double>(Offset[1]) + Sphere[1]);
                Sphere[2] = static_cast<float>(static_cast<double>(Offset[2]) + Sphere[2]);
            }
            std::memcpy(Sphere + 3, &NewRadius, sizeof(NewRadius));
        }
#pragma float_control(pop)
#pragma optimize("", on)

        // Perp's spawn tail (FUN_06235db0): the squared
        // distance of the spawn velocity from +X, and its Y and Z squares, are each
        // compared against DAT_0624eb68, whose bytes BE 37 86 35 are 0x358637BE,
        // about 1.0e-6.
        float PerpAxisParallelEpsilon()
        {
            const unsigned long Bits = 0x358637BEu;
            float Value = 0.0f;
            std::memcpy(&Value, &Bits, sizeof(Value));
            return Value;
        }

        // Pipeline calls FxPerpAppearance::DrawOrEnqueue makes (FUN_06233170): SetTexture at
        // vtable+0x108 and SetTransform at vtable+0x7c.
        void CallRenderPipelineSetTexture(IRenderPipeline* Pipeline, int Stage, int Handle)
        {
            if (Pipeline == nullptr)
                return;
            Pipeline->SetTextureStageTexture(
                Stage, reinterpret_cast<void*>(static_cast<ptrdiff_t>(Handle)));
        }

        void CallRenderPipelineSetTransform(IRenderPipeline* Pipeline, const Transform* Xform)
        {
            if (Pipeline == nullptr || Xform == nullptr)
                return;
            Pipeline->SetModelview(Xform);
        }

        // {Pipeline, State, Saved} render-state saver of FUN_06206850 (stack +0x8/+0xc/+0x10):
        // the constructor is inlined there; the destructor is FUN_06208a20, reached both from
        // the normal exit and from the EH unwind.
        struct FScopedRenderState
        {
            IRenderPipeline* Pipeline;
            int State;
            int Saved;

            FScopedRenderState(IRenderPipeline* InPipeline, int InState)
                : Pipeline(InPipeline), State(InState), Saved(0)
            {
                CallGetRenderState(Pipeline, State, &Saved);
            }

            ~FScopedRenderState()
            {
                CallSetRenderState(Pipeline, State, Saved);
            }

            FScopedRenderState(const FScopedRenderState&) = delete;
            FScopedRenderState& operator=(const FScopedRenderState&) = delete;
        };

        // ---- FxBasicAppearance fill helpers (FUN_06216550 / FUN_06216f20) ----

        float FloatFromBits(unsigned long Bits)
        {
            float Value = 0.0f;
            std::memcpy(&Value, &Bits, sizeof(Value));
            return Value;
        }

        // 0x3d088880 (DAT_0624cdd8, ~1/30): the streak's backward shift per unit of
        // camera-space speed along the screen axis.
        float StreakShiftScale()
        {
            return FloatFromBits(0x3d088880u);
        }

        // 0x3f5db3d7 (DAT_0624cddc) = sqrt(3)/2: the triangle's lower corners.
        float TriangleCornerScale()
        {
            return FloatFromBits(0x3f5db3d7u);
        }

        // Math engine vt+0x28 TransformXform / vt+0x2c TransformMatrix, the calls both fills
        // make through the lazily cached engine DAT_06257a28.
        void CameraSpacePoint(const Transform* ViewWorld, const float* Point, float* Out)
        {
            Acquire3DMath()->TransformXform(*reinterpret_cast<Vector*>(Out),
                                            *ViewWorld,
                                            *reinterpret_cast<const Vector*>(Point));
        }

        void CameraSpaceVector(const Transform* ViewWorld, const float* Vec, float* Out)
        {
            Acquire3DMath()->TransformMatrix(*reinterpret_cast<Vector*>(Out),
                                             ViewWorld->Rotation,
                                             *reinterpret_cast<const Vector*>(Vec));
        }

        // Frame block {texture, U0, V0, U1, V1}: descriptor flag 0x1000 swaps U0/U1, 0x2000
        // swaps V0/V1 (in both the quad and the triangle fill).
        void FlipBasicAppearanceFrame(unsigned long Flags, float* FrameBlock)
        {
            if ((Flags & kAppFlagFlipTexU) != 0)
            {
                std::uint32_t Previous;
                std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
                FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
                std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
            }
            if ((Flags & kAppFlagFlipTexV) != 0)
            {
                std::uint32_t Previous;
                std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
                FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
                std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
            }
        }

        void ApplyFrameFlips(unsigned long Flags, float* FrameBlock)
        {
            if ((Flags & kAppFlagFlipTexU) != 0)
            {
                const float Swap = FrameBlock[1];
                FrameBlock[1] = FrameBlock[3];
                FrameBlock[3] = Swap;
            }
            if ((Flags & kAppFlagFlipTexV) != 0)
            {
                const float Swap = FrameBlock[2];
                FrameBlock[2] = FrameBlock[4];
                FrameBlock[4] = Swap;
            }
        }

        // FUN_06216f20's per-corner UV table: flag 1 selects
        // DAT_06255cc4.. (U 0,0,1  V 1,0,1), otherwise DAT_06255cdc.. (U 0.5,0,1  V 0,1,1).
        void LoadTriangleUvTable(unsigned long Flags, float* TriU, float* TriV)
        {
            if ((Flags & 1u) != 0)
            {
                TriU[0] = 0.0f;
                TriU[1] = 0.0f;
                TriU[2] = 1.0f;
                TriV[0] = 1.0f;
                TriV[1] = 0.0f;
                TriV[2] = 1.0f;
            }
            else
            {
                TriU[0] = 0.5f;
                TriU[1] = 0.0f;
                TriU[2] = 1.0f;
                TriV[0] = 0.0f;
                TriV[1] = 1.0f;
                TriV[2] = 1.0f;
            }
        }

        // Table into the frame rect.
        void MapTriangleUvTable(const float* FrameBlock, float* TriU, float* TriV,
                                bool bInitial, bool bMap = true)
        {
            const volatile double SpanU = static_cast<double>(FrameBlock[3]) - FrameBlock[1];
            const double SpanVUnrounded = static_cast<double>(FrameBlock[4]) - FrameBlock[2];
            // The initial pass stores the V span as float; the per-frame pass keeps it wide.
            const volatile float InitialSpanV = bInitial ? static_cast<float>(SpanVUnrounded) : 0.0f;
            const double SpanV = bInitial ?
                static_cast<double>(InitialSpanV) : SpanVUnrounded;
            if (!bMap)
                return;
            for (int Corner = 0; Corner < 3; ++Corner)
                TriU[Corner] = static_cast<float>(TriU[Corner] * SpanU + FrameBlock[1]);
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                TriV[Corner] = static_cast<float>(
                    static_cast<double>(TriV[Corner]) * SpanV + FrameBlock[2]);
            }
        }

        // Every fill writes into its OWN class's draw context (basic DAT_062579dc, rect
        // DAT_06257b40, perp DAT_06257aec, oriented DAT_06257a74, dust DAT_0625797c, beam
        // DAT_0625792c). The beam calls FUN_06208aa0 / FUN_06208b00 / FUN_06208b50 with ECX =
        // its context; the other classes inline the same stores against their context's
        // globals. The vertex store is written only while the counter is not past the
        // requested count, and the index the caller keeps is
        // the counter before the increment -- or the requested count when the store was
        // skipped (AlSubmitVertex = FUN_06208aa0).
        unsigned short StoreDrawVertex(FAlDrawContext& Context, float X, float Y, float Z,
                                       unsigned long Diffuse, float U, float V)
        {
            return AlSubmitVertex(Context, X, Y, Z, Diffuse, U, V);
        }

        // Quad indices as the list classes inline them: (I3,I2,I1) (I1,I2,I0), skipped when
        // IndexCount + 5 > requested; the primitive counter (+0xc) += 2 -- FUN_06208b50 with
        // the order reversed.
        void StoreQuadIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                              unsigned short I2, unsigned short I3)
        {
            AlSubmitQuadIndices(Context, I3, I2, I1, I0);
        }

        // Triangle indices: (I2,I1,I0), skipped when IndexCount + 2 > requested; +0xc += 1 --
        // FUN_06208b00 with the order reversed.
        void StoreTriangleIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                                  unsigned short I2)
        {
            AlSubmitTriangleIndices(Context, I2, I1, I0);
        }

        // FUN_06208b00 / the beam's inline triangle stores: indices (A, B, C) in that order.
        void StoreBeamTriangle(unsigned short A, unsigned short B, unsigned short C)
        {
            AlSubmitTriangleIndices(G_BeamDrawContext, A, B, C);
        }

        // FUN_06208b50 on the beam context: indices (A, B, C, C, B, D).
        void StoreBeamQuad(unsigned short A, unsigned short B, unsigned short C, unsigned short D)
        {
            AlSubmitQuadIndices(G_BeamDrawContext, A, B, C, D);
        }
    } // namespace

    // ---- The property sets' tables ----
    namespace
    {
        typedef FAppearanceRampDescriptor FSet;

        // Bool tables (+0x04 / +0x08, the getter and the setter test the same hashes in the
        // same order): basic FUN_06215930 / FUN_06215a00, rect FUN_06237c00 / FUN_06237cd0,
        // perp FUN_06235320 / FUN_06232730, oriented FUN_0622e680 / FUN_0622b8a0, dust
        // FUN_06211e10 / FUN_06211e70, beam FUN_06204af0 / FUN_06204ba0.
        const FDescriptorFlagBit kBasicFlags[] = {
            {0xF863872Eu, kAppFlagTriTexture},         {0xF87B5FD5u, kAppFlagQuadTexture},
            {0xE91467F1u, kAppFlagMotionBlur},         {0xE2F60EEBu, kAppFlagUseCommonTexFrame},
            {0xF3D52EE4u, kAppFlagFlipTexU},           {0xE8DC7F5Eu, kAppFlagFlipTexV},
        };
        const FDescriptorFlagBit kRectFlags[] = {
            {0xFB7958C1u, kAppFlagCenterOnPos},        {0xE91467F1u, kAppFlagMotionBlur},
            {0xF9FEBF0Du, kAppFlagViewingAngleFade},   {0xE2F60EEBu, kAppFlagUseCommonTexFrame},
            {0xF3D52EE4u, kAppFlagFlipTexU},           {0xE8DC7F5Eu, kAppFlagFlipTexV},
        };
        const FDescriptorFlagBit kPerpFlags[] = {
            {0xF9FEBF0Du, kAppFlagViewingAngleFade},
            {0xE2F60EEBu, kAppFlagUseCommonTexFrame},
            {0xF3D52EE4u, kAppFlagFlipTexU},
            {0xE8DC7F5Eu, kAppFlagFlipTexV},
        };
        const FDescriptorFlagBit kOrientedFlags[] = {
            {0xF9FEBF0Du, kAppFlagOrientedViewingAngleFade},
            {0xE2F60EEBu, kAppFlagUseCommonTexFrame},
            {0xF3D52EE4u, kAppFlagFlipTexU},
            {0xE8DC7F5Eu, kAppFlagFlipTexV},
        };
        const FDescriptorFlagBit kDustFlags[] = {
            {0xE2F60EEBu, kAppFlagUseCommonTexFrame},
            {0xF3D52EE4u, kAppFlagFlipTexU},
            {0xE8DC7F5Eu, kAppFlagFlipTexV},
        };
        // 0x03503B61 / 0xE2F60EEB / 0x0ABE0402 share the tail that reads 0 or leaves Flags
        // alone and returns 0.
        const FDescriptorFlagBit kBeamFlags[] = {
            {0x08508C35u, kAppFlagBeamDisablePlaceHolder}, {0xE8B8CDE7u, kAppFlagBeamDupeFirstParticle},
            {0x1C65B7B9u, kAppFlagBeamLineAppearance},     {0xF87B5FD5u, kAppFlagQuadTexture},
            {0x03503B61u, 0x0},   {0xE2F60EEBu, 0x0},
            {0x0ABE0402u, 0x0},
        };

        // Kind 0x200 getters (+0x3c; outer offsets of the fields in the comments): basic
        // FUN_06215b50, rect FUN_06237e20, perp FUN_062354d0, oriented FUN_0622b990, dust
        // FUN_06212010, beam FUN_06204cb0.
        const FBillboardRampSlot kBasicSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},           // +0x70
            {0xF7C2EBA9u, &FSet::PrimarySizeRamp},    // +0x78
            {0x05DB630Eu, &FSet::TexFrameRamp},       // +0x7c
            {0x015A6F47u, &FSet::AxisOverrideRamp},   // +0x80
            {0xE0E97650u, &FSet::CommonTexFrameRamp}, // +0x88
        };
        const FBillboardRampSlot kRectSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},         // +0x70
            {0xFBA203B8u, &FSet::PrimarySizeRamp},  // +0x78
            {0xE48DDAE2u, &FSet::TexFrameRamp},     // +0x7c
            {0x1266735Fu, &FSet::AxisOverrideRamp}, // +0x80
            {0x015A6F47u, &FSet::FrameRamp},        // +0x84
        };
        const FBillboardRampSlot kPerpSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},        // +0x70
            {0xF7C2EBA9u, &FSet::PrimarySizeRamp}, // +0x78
            {0x015A6F47u, &FSet::TexFrameRamp},    // +0x7c
        };
        const FBillboardRampSlot kOrientedSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},         // +0x70
            {0xFBA203B8u, &FSet::PrimarySizeRamp},  // +0x78
            {0xF267B8E1u, &FSet::TexFrameRamp},     // +0x7c
            {0xFA03992Fu, &FSet::AxisOverrideRamp}, // +0x80
            {0x015A6F47u, &FSet::FrameRamp},        // +0x84
        };
        const FBillboardRampSlot kBeamSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},           // +0x70
            {0xFBA203B8u, &FSet::PrimarySizeRamp},    // +0x78
            {0xE48DDAE2u, &FSet::CommonTexFrameRamp}, // +0x88
            {0x1266735Fu, &FSet::TexFrameRamp},       // +0x7c
            {0x015A6F47u, &FSet::FrameRamp},          // +0x84
        };

        // Kind 0x202 getters (+0x40), BasicApp_CommonTexFrame 0xF52B8DD5 in all six: basic
        // FUN_06215900 (+0x84), rect / oriented (+0x88), perp / dust / beam (+0x80).
        const FBillboardRampSlot kBasicAnimatedRamps[] = {{0xF52B8DD5u, &FSet::FrameRamp}};
        const FBillboardRampSlot kCommonTexFrameAnimatedRamps[] = {
            {0xF52B8DD5u, &FSet::CommonTexFrameRamp}};
        const FBillboardRampSlot kAxisOverrideAnimatedRamps[] = {
            {0xF52B8DD5u, &FSet::AxisOverrideRamp}};

        // The class tables (+0x54, after the base table): basic 0x06255b70, rect
        // 0x06256aa0, perp 0x06256890, oriented 0x062565b8, dust 0x06255918, beam
        // 0x062555c0, particle 0x06256780, mesh 0x06256298.
        const FNodePropertyRow kBasicPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, kFxPropSingleRampRamp, 0.0f, 100.0f},
            {"BasicApp_TriTexture", 0xF863872EU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_QuadTexture", 0xF87B5FD5U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_MotionBlur", 0xE91467F1U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, kFxPropColorRampRamp, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_Size", 0xF7C2EBA9U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"BasicApp_HToVAspect", 0xE0E97650U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"BasicApp_Rotate", 0x05DB630EU, kFxPropSingleRampRamp, -kNodePropertyRowMax, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, kFxPropName, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, kFxPropPair, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, kFxPropAnimatedSingleRamp, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kRectPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, kFxPropSingleRampRamp, 0.0f, 100.0f},
            {"BasicApp_MotionBlur", 0xE91467F1U, kFxPropBool, 0.0f, 0.0f},
            {"RectApp_CenterOnPos", 0xFB7958C1U, kFxPropBool, 0.0f, 0.0f},
            {"RectApp_ViewingAngleFade", 0xF9FEBF0DU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, kFxPropColorRampRamp, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"RectApp_Scale", 0xFBA203B8U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"RectApp_Length", 0xE48DDAE2U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"RectApp_Width", 0x1266735FU, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, kFxPropName, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, kFxPropPair, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, kFxPropAnimatedSingleRamp, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kPerpPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, kFxPropSingleRampRamp, 0.0f, 100.0f},
            {"RectApp_ViewingAngleFade", 0xF9FEBF0DU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, kFxPropColorRampRamp, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_Size", 0xF7C2EBA9U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, kFxPropName, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, kFxPropPair, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, kFxPropAnimatedSingleRamp, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kOrientedPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, kFxPropSingleRampRamp, 0.0f, 100.0f},
            {"RectApp_ViewingAngleFade", 0xF9FEBF0DU, kFxPropBool, 0.0f, 0.0f},
            {"MeshApp_ParticleTransform", 0xF0004D58U, kFxPropTransform, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, kFxPropColorRampRamp, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"RectApp_Scale", 0xFBA203B8U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"OrientedApp_Height", 0xF267B8E1U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"OrientedApp_Width", 0xFA03992FU, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, kFxPropName, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, kFxPropPair, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, kFxPropAnimatedSingleRamp, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kDustPropertyRows[] = {
            {"BasicApp_Color", 0x048767E8U, kFxPropColorRampRamp, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_Size", 0xF7C2EBA9U, kFxPropSingleRampRamp, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, kFxPropName, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, kFxPropPair, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, kFxPropAnimatedSingleRamp, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, kFxPropBool, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kBeamPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, kFxPropSingleRampRamp, 0.0f, 100.0f},
            {"BasicApp_Color", 0x048767E8U, kFxPropColorRampRamp, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"RectApp_Scale", 0xFBA203B8U, kFxPropSingleRampRamp, 0.0f, 1000.0f},
            {"RectApp_Width", 0x1266735FU, kFxPropSingleRampRamp, 0.0f, 1001.0f},
            {"BasicApp_TexName", 0x1BA23359U, kFxPropName, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, kFxPropPair, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, kFxPropSingleRampRamp, 0.0f, 1.0f},
            {"BasicApp_QuadTexture", 0xF87B5FD5U, kFxPropBool, 0.0f, 0.0f},
            {"BeamApp_DisablePlaceHolder", 0x08508C35U, kFxPropBool, 0.0f, 0.0f},
            {"BeamApp_DupeFirstParticle", 0xE8B8CDE7U, kFxPropBool, 0.0f, 0.0f},
            {"BeamApp_LineAppearance", 0x1C65B7B9U, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kParticlePropertyRows[] = {
            {"ParticleApp_LifeName", 0x0EC7A290U, kFxPropName, 0.0f, 0.0f},
            {"ParticleApp_DeathName", 0x03B76E6CU, kFxPropName, 0.0f, 0.0f},
            {"ParticleApp_UseDynamicRotation", 0xE34C3C55U, kFxPropBool, 0.0f, 0.0f},
            {"ParticleApp_SmoothRotation", 0x049A6DBEU, kFxPropBool, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kMeshPropertyRows[] = {
            {"MeshApp_MeshId", 0x0B7EE41CU, kFxPropInt, 0.0f, 0.0f},
            {"MeshApp_MeshName", 0x18DADCA8U, kFxPropName, 0.0f, 0.0f},
            {"MeshApp_UseParticleTransform", 0x001FC112U, kFxPropBool, 0.0f, 0.0f},
            {"MeshApp_ParticleTransform", 0xF0004D58U, kFxPropTransform, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };

        template <typename T, unsigned long N>
        constexpr unsigned long CountOf(const T (&)[N])
        {
            return N;
        }

        // +0x50: basic / rect / perp / dust are FUN_0623fb50 itself; oriented FUN_0622e640
        // (0xF0004D58) and beam FUN_0620db40 (0x0BA0B3BB) hand out LocalTransform first.
        const FBillboardPropertyClass kBasicClass = {
            kBasicFlags, CountOf(kBasicFlags), kBasicSingleRamps, CountOf(kBasicSingleRamps),
            kBasicAnimatedRamps, CountOf(kBasicAnimatedRamps), 0, kBasicPropertyRows};
        const FBillboardPropertyClass kRectClass = {
            kRectFlags, CountOf(kRectFlags), kRectSingleRamps, CountOf(kRectSingleRamps),
            kCommonTexFrameAnimatedRamps, CountOf(kCommonTexFrameAnimatedRamps), 0,
            kRectPropertyRows};
        const FBillboardPropertyClass kPerpClass = {
            kPerpFlags, CountOf(kPerpFlags), kPerpSingleRamps, CountOf(kPerpSingleRamps),
            kAxisOverrideAnimatedRamps, CountOf(kAxisOverrideAnimatedRamps), 0,
            kPerpPropertyRows};
        const FBillboardPropertyClass kOrientedClass = {
            kOrientedFlags, CountOf(kOrientedFlags), kOrientedSingleRamps,
            CountOf(kOrientedSingleRamps), kCommonTexFrameAnimatedRamps,
            CountOf(kCommonTexFrameAnimatedRamps), 0xF0004D58u, kOrientedPropertyRows};
        const FBillboardPropertyClass kDustClass = {
            kDustFlags, CountOf(kDustFlags), kPerpSingleRamps, CountOf(kPerpSingleRamps),
            kAxisOverrideAnimatedRamps, CountOf(kAxisOverrideAnimatedRamps), 0,
            kDustPropertyRows};
        const FBillboardPropertyClass kBeamClass = {
            kBeamFlags, CountOf(kBeamFlags), kBeamSingleRamps, CountOf(kBeamSingleRamps),
            kAxisOverrideAnimatedRamps, CountOf(kAxisOverrideAnimatedRamps), 0x0BA0B3BBu,
            kBeamPropertyRows};

        // Particle FUN_06231930 / FUN_06231980 and the names FUN_06231ad0 / FUN_06231b20.
        const unsigned long kParticleUseDynamicRotationHash = 0xE34C3C55u;
        const unsigned long kParticleSmoothRotationHash = 0x049A6DBEu;
        const unsigned long kParticleLifeNameHash = 0x0EC7A290u;
        const unsigned long kParticleDeathNameHash = 0x03B76E6Cu;
        // The billboard sets' shared hashes.
        const unsigned long kBillboardTexNameHash = 0x1BA23359u;
        const unsigned long kBillboardBlendInfoHash = 0x1506EB6Cu;
        const unsigned long kBillboardColorHash = 0x048767E8u;
        // DAT_06255804: the name every texture key and the TexName getter hand out.
        const char kTexNameUnknown[] = "???";
        // Mesh FUN_06226b80.. (set 0x0624dff0).
        const unsigned long kMeshUseParticleTransformHash = 0x001FC112u;
        const unsigned long kMeshIdHash = 0x0B7EE41Cu;
        const unsigned long kMeshNameHash = 0x18DADCA8u;
        const unsigned long kMeshParticleTransformHash = 0xF0004D58u;
    } // namespace

    // ---- FFxBillboardPropertySet ----

    FFxBillboardPropertySet::FFxBillboardPropertySet(const FBillboardPropertyClass* PropertyClass)
        : PropertyClass_(PropertyClass)
    {
    }

    // Bool get / set (basic FUN_06215930 / FUN_06215a00, beam FUN_06204af0 / FUN_06204ba0,
    // dust 06211e10 / 06211e70, oriented 0622e680 / 0622b8a0, perp 06235320 / 06232730,
    // rect 06237c00 / 06237cd0): the first row with the hash wins; the setter tests the
    // value's low byte. A row with mask 0 reads 0 and ignores a set.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropBool(unsigned long Hash, unsigned char* Out)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->FlagCount; ++Index)
        {
            const FDescriptorFlagBit& Row = PropertyClass_->Flags[Index];
            if (Row.Hash != Hash)
                continue;
            *Out = (Flags & Row.Mask) != 0 ? 1 : 0;
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxBillboardPropertySet::SetPropBool(unsigned long Hash, unsigned char Value)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->FlagCount; ++Index)
        {
            const FDescriptorFlagBit& Row = PropertyClass_->Flags[Index];
            if (Row.Hash != Hash)
                continue;
            if (Value != 0)
                Flags |= Row.Mask;
            else
                Flags &= ~Row.Mask;
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // Basic FUN_06218b60 / FUN_06218b90 (beam 0620dc00 / 0620dc30, dust 06211f50 /
    // 06211f80, oriented 0622e770 / 0622e7a0, perp 06235410 / 06235440, rect 0623a9d0 /
    // 0623aa00).
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropPair(unsigned long Hash, unsigned long* Out2)
    {
        if (Hash != kBillboardBlendInfoHash)
            return GR_ALE_CREATE_FAIL;
        Out2[0] = BlendInfo[0];
        Out2[1] = BlendInfo[1];
        return GR_OK;
    }

    GENRESULT DACOM_API FFxBillboardPropertySet::SetPropPair(unsigned long Hash,
                                                             const unsigned long* In2)
    {
        if (Hash != kBillboardBlendInfoHash)
            return GR_ALE_CREATE_FAIL;
        BlendInfo[0] = In2[0];
        BlendInfo[1] = In2[1];
        return GR_OK;
    }

    // Basic FUN_06218c60 (beam 0620dd00, dust 062120c0, oriented 0622e870, perp 06235580,
    // rect 0623aad0): only the CRC is kept, so TexName reads back as "???".
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropName(unsigned long Hash, const char** Out)
    {
        if (Hash == kBillboardTexNameHash)
        {
            *Out = kTexNameUnknown;
            return GR_OK;
        }
        return FFxNodePropertySet::GetPropName(Hash, Out);
    }

    // Basic FUN_06218c90 and its five twins.
    GENRESULT DACOM_API FFxBillboardPropertySet::SetPropName(unsigned long Hash, const char* Text)
    {
        if (Hash == kBillboardTexNameHash)
        {
            TextureCrc = DACOM_CRC::GetCRC32(Text);
            return GR_OK;
        }
        return FFxNodePropertySet::SetPropName(Hash, Text);
    }

    // Basic FUN_06218bf0 (beam 0620dc90, dust 06211fe0, oriented 0622e800, perp 062354a0,
    // rect 0623aa60).
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropColorRampRamp(unsigned long Hash,
                                                                      IRampColorRamp** Out)
    {
        if (Hash != kBillboardColorHash)
            return GR_ALE_CREATE_FAIL;
        *Out = ColorRamp;
        (*Out)->AddRef();
        return GR_OK;
    }

    // The class's 0x200 getter; past the last case it jumps to FUN_06244330.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropSingleRampRamp(unsigned long Hash,
                                                                       INestedSingleRamp** Out)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->SingleRampCount; ++Index)
        {
            const FBillboardRampSlot& Slot = PropertyClass_->SingleRamps[Index];
            if (Slot.Hash != Hash)
                continue;
            *Out = this->*Slot.Field;
            (*Out)->AddRef();
            return GR_OK;
        }
        return FFxAppearancePropertySet::GetPropSingleRampRamp(Hash, Out);
    }

    // The class's 0x202 getter (basic FUN_06215900).
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropAnimatedSingleRamp(unsigned long Hash,
                                                                           INestedSingleRamp** Out)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->AnimatedRampCount; ++Index)
        {
            const FBillboardRampSlot& Slot = PropertyClass_->AnimatedRamps[Index];
            if (Slot.Hash != Hash)
                continue;
            *Out = this->*Slot.Field;
            (*Out)->AddRef();
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // Beam FUN_0620db40 (oriented FUN_0622e640): LocalTransform (inner +0x88) first, then
    // FUN_0623fb50.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropTransform(unsigned long Hash,
                                                                  IAnimatedTransform** Out)
    {
        if (PropertyClass_->LocalTransformHash != 0 && Hash == PropertyClass_->LocalTransformHash)
        {
            *Out = LocalTransform;
            (*Out)->AddRef();
            return GR_OK;
        }
        return FFxNodePropertySet::GetPropTransform(Hash, Out);
    }

    // Basic FUN_062158d0 / FUN_06218cd0 (beam FUN_0620dd70, dust FUN_06212130, oriented
    // FUN_0622e8e0, perp FUN_062355f0, rect FUN_0623ab40): FUN_06244320 (the base table);
    // a non-zero result is returned, else the class table (FUN_0623f920).
    int DACOM_API FFxBillboardPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                          void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(PropertyClass_->Rows, Callback, Context);
    }

    // ---- FFxBillboardAppearance ----

    FFxBillboardAppearance::FFxBillboardAppearance(const char* PrimaryIid, const char* FactoryName,
                                                   const FBillboardPropertyClass* PropertyClass)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), PropertyClass_(PropertyClass)
    {
    }

    // Retail's draws read the set through node+0xf0 (dust +0xe8), the creators' cache of
    // the set pointer.
    void FFxBillboardAppearance::OnPropertySetChanged()
    {
        Descriptor_ = static_cast<FAppearanceRampDescriptor*>(PropertySet_);
    }

    // Defined with the other descriptor builders further down.
    static void BuildBasicDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildRectDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildPerpDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildOrientedDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildBeamDescriptorRamps(FAppearanceRampDescriptor& Descriptor);

    // The six billboard creators (basic FUN_06215e80, rect FUN_06238150, perp FUN_06232b00,
    // oriented FUN_0622bcc0, dust FUN_0620f6b0, beam FUN_06205000): operator_new, the
    // class's builder, the outer vtable and count 1, then the shared adoption tail (Release
    // the old +0x7c, store and AddRef, Release the creation reference; basic caches the set
    // at +0xf0 and its inner at +0xa8 and +0x80).
    void FFxBillboardAppearance::AdoptBillboardSet(void (*Build)(FAppearanceRampDescriptor&))
    {
        FFxBillboardPropertySet* const Set = new FFxBillboardPropertySet(PropertyClass_);
        Build(*Set);
        AdoptPropertySet(Set);
    }

    FxBasicAppearance::FxBasicAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kBasicClass)
    {
    }

    void FxBasicAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildBasicDescriptorRamps);
    }

    // FUN_06215d80: the basic constructor inline, FUN_06244120, then the +0xf0 set cache
    // (Descriptor_ here).
    GENRESULT DACOM_API FxBasicAppearance::CloneNode(IDAComponent** Out)
    {
        FxBasicAppearance* const Node = new (std::nothrow) FxBasicAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        // Retail reloads the prototype cache after initialization callbacks.
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxBasicAppearance::~FxBasicAppearance()
    {
    }

    // Outer +0x14 -> FUN_06216420. Rect, perp, oriented and dust repeat this body with
    // their own draw counters, frame ramp and pool.
    GENRESULT DACOM_API FxBasicAppearance::UpdateNode(float Dt)
    {
        G_BasicDrawContext.Field_0xA = 0;
        G_BasicDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & kAppFlagUseCommonTexFrame) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->FrameRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                static_cast<ITextureLibrary2*>(G_TextureLibrary)->GetFrame(Texture_.Handle,
                    static_cast<int>(Frame), reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolBasic, Dt);
        return GR_OK;
    }

    // FUN_06216090 (outer +0x8C via the FUN_06219320 thunk). The list tail (with its own
    // t) is merged right after the first particle, then the rest in order; the merge uses
    // FUN_06206ad0's rsqrt length. The centre is finally transformed by node+0x4c.
    GENRESULT DACOM_API FxBasicAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const double FirstRadius = ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime) * Scale;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Tail->InvDuration) * Tail->Age);
            const double TailRadius = ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const double Radius = ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::PerpUnroundedXyz);
            }
        }
        Vector WorldCenter;
        TransformPerpBoundsCenter(NodeWorldXform_,
            WorldCenter, *reinterpret_cast<const Vector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_06215f40 (thunk LAB_06219640): SpawnListParticle unchanged.
    GENRESULT DACOM_API FxBasicAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolBasic, Six, Age, Duration);
        return GR_OK;
    }

    // FUN_06217640 (node vtable 0x0624cb30), always returns 0. With the batcher on
    // (DAT_06257919) the node is queued on batch list 0 (FUN_06201c70) instead of drawn.
    // Counts come from vt+0xd0/+0xd4, the context is DAT_062579dc (FUN_06240f50), and
    // descriptor flag 2 selects FUN_06216550 over FUN_06216f20. The fills produce
    // camera-space vertices, so the modelview is identity.
    GENRESULT DACOM_API FxBasicAppearance::DrawOrEnqueue()
    {
        if (InstanceListHead_ == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(0, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_BasicDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        ++G_DrawCounters[3];
        if ((Descriptor_->Flags & kAppFlagQuadTexture) != 0)
            FillQuadGeometry();
        else
            FillTriangleGeometry();

        // The rest is the same sequence as the outer +0xA0 body FUN_062193b0.
        EndBatchedDraw();
        return GR_OK;
    }

    // LAB_06217940 (inner) / FUN_06219390 (outer +0x9C).
    GENRESULT DACOM_API FxBasicAppearance::BeginBatchedDraw(unsigned long Verts,
                                                           unsigned long Indices)
    {
        return AlDrawContextBegin(G_BasicDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // FUN_062193b0 (outer +0xA0): blend from the set's BlendInfo (desc+0x68/+0x6c),
    // texture from node+0xbc, identity modelview, FUN_06241180 draw.
    GENRESULT DACOM_API FxBasicAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_BasicDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];

        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));

        // node+0xbc: the first dword of the frame block FUN_06244f30 fills (Texture_.Frame[0]).
        void* FrameTexture = nullptr;
        std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
        G_RenderPipeline->SetTextureStageTexture(0, FrameTexture);

        const Transform IdentityXform = {
            Matrix(1.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f,
                   0.0f, 0.0f, 1.0f),
            {0.0f, 0.0f, 0.0f},
        };
        G_RenderPipeline->SetModelview(&IdentityXform);

        AlDrawContextDraw(G_BasicDrawContext);
        return GR_OK;
    }

    // Outer +0xA4 = LAB_062194b0, node body LAB_06217a60: ++DAT_062578f0, then the same
    // flag-2 dispatch as the immediate draw.
    // The batch flush FUN_06201cc0 calls it between its +0x9C begin and +0xA0 draw.
    GENRESULT DACOM_API FxBasicAppearance::FillBatchedGeometry()
    {
        ++G_DrawCounters[3];
        if ((Descriptor_->Flags & kAppFlagQuadTexture) != 0)
            FillQuadGeometry();
        else
            FillTriangleGeometry();
        return GR_OK;
    }

    // FUN_06216550, __thiscall on the node. A Scale of exactly 0 returns (NaN does not).
    // Particles with C.z > 0 (strictly) or an ordered Alpha < 0.01 are skipped.
    //   Streak (flag 4): V = the velocity in camera space; U = (C x V) x C normalized with
    //     one Newton step; the base point is shifted back by (V . U) * 0x3d088880;
    //     corners (B-U-P, C+U-P, B-U+P, C+U+P), UVs (U1,V0) (U1,V1) (U0,V0) (U0,V1).
    //   Plain: corners C+W+H, C+W-H, C-W+H, C-W-H, UVs (U1,V0) (U0,V0) (U1,V1) (U0,V1).
    // The ramps are not null-checked, as in retail: FUN_06215570 builds them all.
    void FxBasicAppearance::FillQuadGeometry()
    {
        const FPooledParticle* Particle = InstanceListHead_;
        const double ScaleValue = ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_);
        const float Scale = static_cast<float>(ScaleValue);
        if (IsZeroBasicScale(ScaleValue))
            return;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & kAppFlagUseCommonTexFrame) == 0;
        const bool bStreak = (Descriptor_->Flags & kAppFlagMotionBlur) != 0;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        Transform ViewWorld;
        ComposeTransform(&ViewWorld, Camera->GetViewTransform(), NodeWorldXform_);
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        FlipBasicAppearanceFrame(Descriptor_->Flags, FrameBlock);
        std::int32_t LastFrame = -1;
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            Vector C;
            Shared3DMath()->TransformXform(C,
                ViewWorld,
                *reinterpret_cast<const Vector*>(&Particle->PosX));
            if (C.z > 0.0f)
                continue;
            float OuterTime;
            std::memcpy(&OuterTime, &AppearanceOuterTime_, sizeof(float));
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, OuterTime, InnerTime));
            const float Size = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            if (Alpha < FloatFromBits(0x3c23d70au))
                continue;
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            const float Angle = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
            const float Aspect = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->CommonTexFrameRamp, AppearanceOuterTime_, InnerTime));
            float Trig[2];
            ForwardAppearanceSinCos(Angle, Trig);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const std::uint32_t CountBits = Texture_.FrameCount - 1u;
                std::int32_t SignedCount;
                std::memcpy(&SignedCount, &CountBits, sizeof(SignedCount));
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * SignedCount);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    FlipBasicAppearanceFrame(Descriptor_->Flags, FrameBlock);
                }
            }
            float Bx = 0.0f;
            float By = 0.0f;
            float Ux;
            float Uy;
            float Px;
            float Py;
            if (bStreak)
            {
                Vector V;
                Shared3DMath()->TransformMatrix(V,
                    ViewWorld.Rotation,
                    *reinterpret_cast<const Vector*>(&Particle->VelX));
                const double N0 = static_cast<double>(C.y) * V.z - static_cast<double>(V.y) * C.z;
                const double N1 = static_cast<double>(V.x) * C.z - static_cast<double>(C.x) * V.z;
                const double N2 = static_cast<double>(C.x) * V.y - static_cast<double>(V.x) * C.y;
                const float Ua = static_cast<float>(static_cast<double>(C.z) * N1 - static_cast<double>(C.y) * N2);
                const float Ub = static_cast<float>(static_cast<double>(C.x) * N2 - static_cast<double>(C.z) * N0);
                const double Uc = N0 * C.y - static_cast<double>(C.x) * N1;
                const double SquareB = static_cast<double>(Ub) * Ub;
                const double SquareA = static_cast<double>(Ua) * Ua;
                const double SumBa = SquareB + SquareA;
                const float LengthSquared = static_cast<float>(SumBa + Uc * Uc);
                std::int32_t LengthBits;
                std::memcpy(&LengthBits, &LengthSquared, sizeof(LengthBits));
                const std::uint32_t GuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(LengthBits >> 1);
                float Guess;
                std::memcpy(&Guess, &GuessBits, sizeof(Guess));
                const double InverseLength = (1.5 - (((static_cast<double>(Guess) * Guess) * LengthSquared) * 0.5)) * Guess;
                const float NormalX = static_cast<float>(static_cast<double>(Ua) * InverseLength);
                const float NormalY = static_cast<float>(static_cast<double>(Ub) * InverseLength);
                const double NormalZ = Uc * InverseLength;
                const double DotY = static_cast<double>(V.y) * NormalY;
                const double DotX = static_cast<double>(V.x) * NormalX;
                const double SumYx = DotY + DotX;
                const double Shift = (SumYx + static_cast<double>(V.z) * NormalZ) * StreakShiftScale();
                const double ShiftX = static_cast<double>(NormalX) * Shift;
                const float ShiftY = static_cast<float>(Shift * NormalY);
                Bx = static_cast<float>(static_cast<double>(C.x) - ShiftX);
                By = static_cast<float>(static_cast<double>(C.y) - ShiftY);
                Ux = static_cast<float>(static_cast<double>(NormalX) * Size);
                const double UnroundedUy = static_cast<double>(NormalY) * Size;
                Uy = static_cast<float>(UnroundedUy);
                Px = static_cast<float>(-UnroundedUy * Aspect);
                Py = static_cast<float>(static_cast<double>(Ux) * Aspect);
            }
            else
            {
                const double AspectSize = static_cast<double>(Aspect) * Size;
                Px = static_cast<float>(static_cast<double>(Trig[0]) * AspectSize);
                Py = static_cast<float>(static_cast<double>(Trig[1]) * AspectSize);
                Ux = static_cast<float>(static_cast<double>(Trig[0]) * Size);
                Uy = static_cast<float>(static_cast<double>(Trig[1]) * Size);
            }
            unsigned short Indices[4];
            for (unsigned long Corner = 0; Corner < 4; ++Corner)
            {
                const unsigned long UIndex = bStreak ? (Corner < 2 ? 3 : 1) :
                    ((Corner == 0 || Corner == 2) ? 3 : 1);
                const unsigned long VIndex = bStreak ? ((Corner == 0 || Corner == 2) ? 2 : 4) :
                    (Corner < 2 ? 2 : 4);
                double V = 0.0;
                if (Corner == 0)
                    V = LoadAppearanceScalar(FrameBlock[VIndex]);
                const double U = LoadAppearanceScalar(FrameBlock[UIndex]);
                const long Count = G_BasicDrawContext.VertCount;
                Indices[Corner] = static_cast<unsigned short>(G_BasicDrawContext.RequestedVerts);
                if (Count > static_cast<long>(G_BasicDrawContext.RequestedVerts))
                    continue;
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BasicDrawContext.LockedVerts);
                double X;
                if (bStreak)
                    X = Corner == 0 ? (static_cast<double>(Bx) - Ux) - Px :
                        Corner == 1 ? (static_cast<double>(C.x) + Ux) - Px :
                        Corner == 2 ? (static_cast<double>(Bx) - Ux) + Px :
                                      (static_cast<double>(C.x) + Px) + Ux;
                else
                    X = Corner == 0 ? (static_cast<double>(C.x) + Uy) + Px :
                        Corner == 1 ? (static_cast<double>(C.x) - Px) + Uy :
                        Corner == 2 ? (static_cast<double>(C.x) + Px) - Uy :
                                      (static_cast<double>(C.x) - Px) - Uy;
                Vertex->Diffuse = Diffuse;
                if (Corner != 0)
                    std::memcpy(&Vertex->V, &FrameBlock[VIndex], sizeof(float));
                Vertex->X = static_cast<float>(X);
                double Y;
                if (bStreak)
                    Y = Corner == 0 ? (static_cast<double>(By) - Uy) - Py :
                        Corner == 1 ? (static_cast<double>(C.y) + Uy) - Py :
                        Corner == 2 ? (static_cast<double>(By) - Uy) + Py :
                                      (static_cast<double>(C.y) + Py) + Uy;
                else
                    Y = Corner == 0 ? (static_cast<double>(C.y) - Py) + Ux :
                        Corner == 1 ? (static_cast<double>(C.y) + Ux) + Py :
                        Corner == 2 ? (static_cast<double>(C.y) - Py) - Ux :
                                      (static_cast<double>(C.y) + Py) - Ux;
                Vertex->Y = static_cast<float>(Y);
                Vertex->Z = static_cast<float>(LoadAppearanceScalar(C.z));
                Vertex->U = static_cast<float>(U);
                if (Corner == 0)
                    Vertex->V = static_cast<float>(V);
                Indices[Corner] = static_cast<unsigned short>(Count);
                G_BasicDrawContext.LockedVerts = Vertex + 1;
                ++G_BasicDrawContext.VertCount;
            }
            StoreQuadIndices(G_BasicDrawContext, Indices[0], Indices[1], Indices[2], Indices[3]);
        }
    }
    // FUN_06216f20, __thiscall on the node. Unlike the quad: no Scale zero test and no
    // depth test. Retail loads a fourth, never-read UV dword (for flag 1 clear, the first
    // bytes of "BasicApp_Rotate"). Retail quirk kept: the third corner is offset by
    // (sin, cos) * Alpha, not by the size, and the stored Size2 * sqrt(3)/2 is overwritten
    // by the trig output before it is used.
    void FxBasicAppearance::FillTriangleGeometry()
    {
        const FPooledParticle* Particle = InstanceListHead_;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & kAppFlagUseCommonTexFrame) == 0;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const Transform* const View = Camera->GetViewTransform();
        Transform ViewTemporary;
        Shared3DMath()->MulTransform(ViewTemporary,
            *View,
            NodeWorldXform_);
        Transform ViewWorld;
        std::memcpy(&ViewWorld, &ViewTemporary, sizeof(ViewTemporary));
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        const FAppearanceRampDescriptor* const InitialDescriptor = Descriptor_;
        FlipBasicAppearanceFrame(InitialDescriptor->Flags, FrameBlock);
        float TriU[3];
        float TriV[3];
        LoadTriangleUvTable(InitialDescriptor->Flags, TriU, TriV);
        MapTriangleUvTable(FrameBlock, TriU, TriV, true, !bFrameLookup);
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            InitialDescriptor->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        std::int32_t LastFrame = -1;
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const float Size = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            if (Alpha < FloatFromBits(0x3c23d70au))
                continue;
            Vector C;
            Shared3DMath()->TransformXform(C,
                ViewWorld,
                *reinterpret_cast<const Vector*>(&Particle->PosX));
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            const double DoubledSize = static_cast<double>(Size) + Size;
            const volatile float Size2 = static_cast<float>(DoubledSize);
            // The next trig output overwrites this stored product in retail.
            const volatile float RoundedCornerProduct = static_cast<float>(DoubledSize * TriangleCornerScale());
            const float Angle = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
            float Trig[2];
            ForwardAppearanceSinCos(Angle, Trig);
            const float Xa = static_cast<float>(static_cast<double>(Trig[0]) * Size2);
            const float Ya = static_cast<float>(static_cast<double>(Trig[1]) * Size2);
            const float Xb = static_cast<float>(static_cast<double>(Trig[0]) * Size);
            const float Yb = static_cast<float>(static_cast<double>(Trig[1]) * Size);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const std::uint32_t CountBits = Texture_.FrameCount - 1u;
                std::int32_t SignedCount;
                std::memcpy(&SignedCount, &CountBits, sizeof(SignedCount));
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * SignedCount);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    const FAppearanceRampDescriptor* const FrameDescriptor = Descriptor_;
                    FlipBasicAppearanceFrame(FrameDescriptor->Flags, FrameBlock);
                    LoadTriangleUvTable(FrameDescriptor->Flags, TriU, TriV);
                    MapTriangleUvTable(FrameBlock, TriU, TriV, false);
                }
            }
            unsigned short Indices[3];
            for (unsigned long Corner = 0; Corner < 3; ++Corner)
            {
                double ThirdY = 0.0;
                if (Corner == 2)
                    ThirdY = static_cast<double>(Trig[0]) * Alpha + C.y;
                const long Count = G_BasicDrawContext.VertCount;
                Indices[Corner] = static_cast<unsigned short>(G_BasicDrawContext.RequestedVerts);
                if (Count > static_cast<long>(G_BasicDrawContext.RequestedVerts))
                    continue;
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BasicDrawContext.LockedVerts);
                const double X = Corner == 0 ? (static_cast<double>(C.x) + Xa) - Yb :
                    Corner == 1 ? (static_cast<double>(C.x) - Xa) - Yb :
                                  static_cast<double>(Trig[1]) * Alpha + C.x;
                const unsigned long UvIndex = 2 - Corner;
                Vertex->Diffuse = Diffuse;
                std::memcpy(&Vertex->U, &TriU[UvIndex], sizeof(float));
                std::memcpy(&Vertex->V, &TriV[UvIndex], sizeof(float));
                Vertex->X = static_cast<float>(X);
                if (Corner == 2)
                    Vertex->Y = static_cast<float>(ThirdY);
                else
                    Vertex->Y = static_cast<float>(Corner == 0 ?
                        (static_cast<double>(C.y) - Ya) - Xb :
                        (static_cast<double>(C.y) + Ya) - Xb);
                Vertex->Z = static_cast<float>(LoadAppearanceScalar(C.z));
                Indices[Corner] = static_cast<unsigned short>(Count);
                G_BasicDrawContext.LockedVerts = Vertex + 1;
                ++G_BasicDrawContext.VertCount;
            }
            StoreTriangleIndices(G_BasicDrawContext, Indices[0], Indices[1], Indices[2]);
        }
    }
    // ---- The six billboard set builders ----
    // Each runs FUN_06243ff0 first (FFxAppearancePropertySet's constructor: the
    // Appearance_LODCurve ramp at +0x58), zeroes Flags, TextureCrc and its ramp slots,
    // seeds BlendInfo {5, 2} (FAppearanceRampDescriptor's constructor), then builds the
    // ColorRamp (FxRampColorRamp{FUN_06209240({0.8, 0.8, 0.8})}) and its own list; each
    // step first releases what its slot holds (the set is new: nothing). Nested scalar
    // ramps are one-key FxRampSingleRamps unless noted. The colour ramp comes first in all
    // six.
    static void BuildDescriptorHead(FAppearanceRampDescriptor& Descriptor)
    {
        const float Grey[3] = {0.8f, 0.8f, 0.8f};
        MakeRampColorRamp(Grey, &Descriptor.ColorRamp);
    }

    // FUN_06215570.
    static void BuildBasicDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.TexFrameRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.AxisOverrideRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.FrameRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.CommonTexFrameRamp);
        Descriptor.Flags = 0;
    }

    // FUN_06237840, and the ramp part of FUN_0622b3e0.
    static void BuildRectDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.TexFrameRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.AxisOverrideRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.FrameRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.CommonTexFrameRamp);
        Descriptor.Flags = 0;
    }

    // FUN_062323a0 (perp) and FUN_0620ef80 (dust), which build the same set.
    static void BuildPerpDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.TexFrameRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.AxisOverrideRamp);
        Descriptor.Flags = 0;
    }

    // FUN_0622b3e0: the rect list, then a fresh FxAnimatedTransform stored through
    // FUN_062084f0 (AddRef). Retail leak kept: the builder keeps its construction
    // reference, so the count ends at 2 and one is never released.
    static void BuildOrientedDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildRectDescriptorRamps(Descriptor);
        FFxAnimatedTransform* Transform = new FFxAnimatedTransform();
        ReleaseDescriptorSlot(Descriptor.LocalTransform);
        Descriptor.LocalTransform = Transform;
        Transform->AddRef();
    }

    // FUN_06204670. Unlike oriented, the transform (FUN_06208620) is released once after
    // the store, so the descriptor holds the only reference.
    static void BuildBeamDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.CommonTexFrameRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.TexFrameRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.FrameRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.AxisOverrideRamp);
        FFxAnimatedTransform* Transform = new FFxAnimatedTransform();
        ReleaseDescriptorSlot(Descriptor.LocalTransform);
        Descriptor.LocalTransform = Transform;
        Transform->AddRef();
        Transform->Release();
        Descriptor.Flags = 2;
    }

    FxRectAppearance::FxRectAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kRectClass)
    {
    }

    void FxRectAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildRectDescriptorRamps);
    }

    // FUN_06238050: as basic's clone.
    GENRESULT DACOM_API FxRectAppearance::CloneNode(IDAComponent** Out)
    {
        FxRectAppearance* const Node = new (std::nothrow) FxRectAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxRectAppearance::~FxRectAppearance() noexcept(false)
    {
    }

    // Outer +0x14 -> FUN_06238780: the basic update body (FxBasicAppearance::UpdateNode).
    GENRESULT DACOM_API FxRectAppearance::UpdateNode(float Dt)
    {
        G_RectDrawContext.Field_0xA = 0;
        G_RectDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & kAppFlagUseCommonTexFrame) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->CommonTexFrameRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const int Handle = Texture_.Handle;
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                ITextureLibrary2* const Library = static_cast<ITextureLibrary2*>(G_TextureLibrary);
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                Library->GetFrame(Handle, static_cast<int>(Frame),
                    reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolRect, Dt);
        return GR_OK;
    }

    // FUN_06238280: round Primary and B before the next callbacks, but retain C
    // as double for the ordered max and radius. Distance uses a float-stored YZX sum.
    GENRESULT DACOM_API FxRectAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const float FirstPrimary = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime) * Scale);
        const float FirstB = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->TexFrameRamp, AppearanceOuterTime_, FirstInnerTime));
        const double FirstC = ForwardAppearanceSingleEval(
            Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, FirstInnerTime);
        const double FirstSide = static_cast<double>(FirstB) > FirstC ? FirstB : FirstC;
        const double FirstRadius = FirstSide * FirstPrimary;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const float TailPrimary = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale);
            const float TailB = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, TailInnerTime));
            const double TailC = ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, TailInnerTime);
            const double TailSide = static_cast<double>(TailB) > TailC ? TailB : TailC;
            const double TailRadius = TailSide * TailPrimary;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::RectRoundedYzx);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const float Primary = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
                const float B = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
                const double C = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const double Side = static_cast<double>(B) > C ? B : C;
                const double Radius = Side * Primary;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::RectRoundedYzx);
            }
        }
        Vector WorldCenter;
        Shared3DMath()->TransformXform(WorldCenter,
            NodeWorldXform_,
            *reinterpret_cast<const Vector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_0623b410 -> FUN_0623a2e0. See
    // FGenericFxObjectBase::SpawnListParticle.
    GENRESULT DACOM_API FxRectAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolRect, Six, Age, Duration);
        return GR_OK;
    }

    // FUN_062388b0 (node vtable 0x0624eee8 +0x10; outer +0x18 thunk LAB_0623ac50): the
    // same shape as the basic and perp draws; with the batcher on (DAT_06257919) the node
    // goes on batch list 1. Returns 0 on every path.
    GENRESULT DACOM_API FxRectAppearance::DrawOrEnqueue()
    {
        if (InstanceListHead_ == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(1, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_RectDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        FillGeometry();
        EndBatchedDraw();
        return GR_OK;
    }

    // LAB_0623b140 (outer +0x9C): FUN_06240f50 on context DAT_06257b40 (see
    // FxPerpAppearance::BeginBatchedDraw on the single context).
    GENRESULT DACOM_API FxRectAppearance::BeginBatchedDraw(unsigned long Verts,
                                                          unsigned long Indices)
    {
        return AlDrawContextBegin(G_RectDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // FUN_0623b1a0 (outer +0xA0): the perp body FUN_06235a60 on context DAT_06257b40 (math
    // engine cache DAT_06257b8c).
    GENRESULT DACOM_API FxRectAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_RectDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];

        const unsigned long* const Blend = Descriptor_->BlendInfo;
        CallSetRenderState(G_RenderPipeline, D3DRS_SRCBLEND, static_cast<int>(Blend[0]));
        CallSetRenderState(G_RenderPipeline, D3DRS_DESTBLEND, static_cast<int>(Blend[1]));

        unsigned long FrameTexture = 0;
        std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
        CallRenderPipelineSetTexture(G_RenderPipeline, 0, static_cast<int>(FrameTexture));

        Transform ViewWorld;
        ComposeTransform(&ViewWorld, GetDrawCameraTransform(), NodeWorldXform_);
        CallRenderPipelineSetTransform(G_RenderPipeline, &ViewWorld);

        AlDrawContextDraw(G_RectDrawContext);
        return GR_OK;
    }

    // FUN_0623b2a0 (outer +0xA4).
    GENRESULT DACOM_API FxRectAppearance::FillBatchedGeometry()
    {
        FillGeometry();
        return GR_OK;
    }

    // FUN_06238a70: Rect's independent camera transform and full list/quad walk.
#pragma float_control(except, on, push)
    void FxRectAppearance::FillGeometry()
    {
        const float TinyLength = FloatFromBits(0x358637bdu);
        const float TinyAlpha = FloatFromBits(0x38d1b717u);
        const float SpeedStretch = FloatFromBits(0x3d08850au);
        const float FacingCull = FloatFromBits(0x3f7c1c5cu);
        const float FacingFade = FloatFromBits(0x3f5db3d7u);
        const float FacingFadeScale = FloatFromBits(0x4106b33du);
        ++G_DrawCounters[4];
        const FPooledParticle* Particle = InstanceListHead_;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & kAppFlagUseCommonTexFrame) == 0;
        const bool bStretch = (Descriptor_->Flags & kAppFlagMotionBlur) != 0;
        const bool bCentered = (Descriptor_->Flags & 1u) != 0;
        const bool bViewFade = (Descriptor_->Flags & kAppFlagViewingAngleFade) != 0;
        Transform InvNode;
        float InverseW;
        ComputeGeneralInverseTransform(NodeWorldXform_,
            InvNode, InverseW);
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        if ((Descriptor_->Flags & kAppFlagFlipTexU) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
            FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
            std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
        }
        if ((Descriptor_->Flags & kAppFlagFlipTexV) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
            FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
            std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
        }
        std::int32_t LastFrame = -1;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const float* const CameraPosition = (const float*)Camera->GetPosition();
        Vector LocalCamera;
        Shared3DMath()->TransformXform(LocalCamera, InvNode,
            *reinterpret_cast<const Vector*>(CameraPosition));
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            const float Length = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime) * SizeScale);
            const float HalfWidth = static_cast<float>((ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime) * SizeScale) * 0.5);
            // Only an ordered less-or-equal skips; unordered continues.
            if (Length <= TinyLength || HalfWidth <= TinyLength || Alpha <= TinyAlpha)
                continue;
            Vector Position;
            Vector Velocity;
            std::memcpy(&Position, &Particle->PosX, sizeof(Position));
            std::memcpy(&Velocity, &Particle->VelX, sizeof(Velocity));
            const double VelocitySquared = ((static_cast<double>(Velocity.z) * Velocity.z) +
                (static_cast<double>(Velocity.y) * Velocity.y)) + (static_cast<double>(Velocity.x) * Velocity.x);
            const float VelocitySeed = static_cast<float>(VelocitySquared);
            std::int32_t VelocityBits;
            std::memcpy(&VelocityBits, &VelocitySeed, sizeof(VelocityBits));
            const std::uint32_t VelocityGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(VelocityBits >> 1);
            float VelocityGuess;
            std::memcpy(&VelocityGuess, &VelocityGuessBits, sizeof(VelocityGuess));
            const double VelocityInverse = (1.5 - (((static_cast<double>(VelocityGuess) * VelocityGuess) *
                VelocitySquared) * 0.5)) * VelocityGuess;
            const float Speed = static_cast<float>(VelocityInverse * VelocitySquared);
            // Greater and unordered both normalize.
            if (!(Speed <= TinyLength))
            {
                const double InverseSpeed = 1.0 / Speed;
                Velocity.x = static_cast<float>(static_cast<double>(Velocity.x) * InverseSpeed);
                Velocity.y = static_cast<float>(static_cast<double>(Velocity.y) * InverseSpeed);
                Velocity.z = static_cast<float>(InverseSpeed * Velocity.z);
            }
            else
            {
                Velocity.x = 0.0f;
                Velocity.y = 1.0f;
                Velocity.z = 0.0f;
            }
            Vector Direction;
            Direction.z = static_cast<float>(static_cast<double>(Position.z) - LocalCamera.z);
            Direction.y = static_cast<float>(static_cast<double>(Position.y) - LocalCamera.y);
            Direction.x = static_cast<float>(static_cast<double>(Position.x) - LocalCamera.x);
            if (bViewFade)
            {
                const double DirectionSquared = ((static_cast<double>(Direction.z) * Direction.z) +
                    (static_cast<double>(Direction.x) * Direction.x)) + (static_cast<double>(Direction.y) * Direction.y);
                const float DirectionSeed = static_cast<float>(DirectionSquared);
                std::int32_t DirectionBits;
                std::memcpy(&DirectionBits, &DirectionSeed, sizeof(DirectionBits));
                const std::uint32_t DirectionGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(DirectionBits >> 1);
                float DirectionGuess;
                std::memcpy(&DirectionGuess, &DirectionGuessBits, sizeof(DirectionGuess));
                const double DirectionInverse = (1.5 - (((static_cast<double>(DirectionGuess) * DirectionGuess) *
                    DirectionSquared) * 0.5)) * DirectionGuess;
                const double DirectionLength = DirectionInverse * DirectionSquared;
                if (!(DirectionLength <= TinyLength))
                {
                    const double InverseDirection = 1.0 / DirectionLength;
                    Direction.x = static_cast<float>(static_cast<double>(Direction.x) * InverseDirection);
                    Direction.y = static_cast<float>(static_cast<double>(Direction.y) * InverseDirection);
                    Direction.z = static_cast<float>(InverseDirection * Direction.z);
                }
                else
                {
                    Direction.x = 0.0f;
                    Direction.y = 1.0f;
                    Direction.z = 0.0f;
                }
                const double UnroundedFacing = std::fabs(((static_cast<double>(Direction.z) * Velocity.z) +
                    (static_cast<double>(Velocity.y) * Direction.y)) + (static_cast<double>(Direction.x) * Velocity.x));
                const float Facing = static_cast<float>(UnroundedFacing);
                // Only an ordered greater skips, tested on the unrounded value.
                if (UnroundedFacing > FacingCull)
                    continue;
                if (Facing > FacingFade)
                {
                    const double Fade = (static_cast<double>(FacingCull) - Facing) * FacingFadeScale;
                    Alpha = static_cast<float>((static_cast<double>(Alpha) * Fade) * Fade);
                }
                if (Alpha <= TinyAlpha)
                    continue;
            }
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0f);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->FrameRamp, AppearanceOuterTime_, InnerTime);
                const std::int32_t Span = static_cast<std::int32_t>(Texture_.FrameCount - 1u);
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * Span);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    if ((Descriptor_->Flags & kAppFlagFlipTexU) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
                        FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
                        std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
                    }
                    if ((Descriptor_->Flags & kAppFlagFlipTexV) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
                        FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
                        std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
                    }
                }
            }
            Vector P0;
            Vector P1;
            if (bCentered)
            {
                const double Half = bStretch ? ((static_cast<double>(Speed) * SpeedStretch) + Length) * 0.5 :
                    static_cast<double>(Length) * 0.5;
                const float ExtentX = static_cast<float>(static_cast<double>(Velocity.x) * Half);
                const float ExtentY = static_cast<float>(static_cast<double>(Velocity.y) * Half);
                const float ExtentZ = static_cast<float>(Half * Velocity.z);
                P0.z = static_cast<float>(static_cast<double>(Position.z) - ExtentZ);
                P0.y = static_cast<float>(static_cast<double>(Position.y) - ExtentY);
                P0.x = static_cast<float>(static_cast<double>(Position.x) - ExtentX);
                P1.x = static_cast<float>(static_cast<double>(ExtentX) + Position.x);
                P1.y = static_cast<float>(static_cast<double>(ExtentY) + Position.y);
                P1.z = static_cast<float>(static_cast<double>(ExtentZ) + Position.z);
            }
            else
            {
                std::memcpy(&P0, &Position, sizeof(P0));
                if (bStretch)
                {
                    const double Extent = (static_cast<double>(Speed) * SpeedStretch) + Length;
                    const double ExtentX = static_cast<double>(Velocity.x) * Extent;
                    const float ExtentY = static_cast<float>(Extent * Velocity.y);
                    const float ExtentZ = static_cast<float>(Extent * Velocity.z);
                    P1.x = static_cast<float>(ExtentX + Position.x);
                    P1.y = static_cast<float>(static_cast<double>(ExtentY) + Position.y);
                    P1.z = static_cast<float>(static_cast<double>(ExtentZ) + Position.z);
                }
                else
                {
                    const double ExtentX = static_cast<double>(Velocity.x) * Length;
                    const double ExtentY = static_cast<double>(Velocity.y) * Length;
                    const float ExtentZ = static_cast<float>(static_cast<double>(Velocity.z) * Length);
                    P1.x = static_cast<float>(ExtentX + Position.x);
                    P1.y = static_cast<float>(ExtentY + Position.y);
                    P1.z = static_cast<float>(static_cast<double>(ExtentZ) + Position.z);
                }
            }
            const double NormalX = (static_cast<double>(Direction.z) * Velocity.y) -
                (static_cast<double>(Velocity.z) * Direction.y);
            const float NormalY = static_cast<float>((static_cast<double>(Velocity.z) * Direction.x) -
                (static_cast<double>(Direction.z) * Velocity.x));
            const double NormalZ = (static_cast<double>(Direction.y) * Velocity.x) -
                (static_cast<double>(Direction.x) * Velocity.y);
            const double NormalSquared = ((NormalX * NormalX) + (static_cast<double>(NormalY) * NormalY)) +
                (NormalZ * NormalZ);
            const float NormalSeed = static_cast<float>(NormalSquared);
            std::int32_t NormalBits;
            std::memcpy(&NormalBits, &NormalSeed, sizeof(NormalBits));
            const std::uint32_t NormalGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(NormalBits >> 1);
            float NormalGuess;
            std::memcpy(&NormalGuess, &NormalGuessBits, sizeof(NormalGuess));
            const double NormalInverse = (1.5 - (((static_cast<double>(NormalGuess) * NormalGuess) *
                NormalSquared) * 0.5)) * NormalGuess;
            const double NormalLength = NormalInverse * NormalSquared;
            if (NormalLength <= TinyLength)
                continue;
            const double WidthScale = static_cast<double>(HalfWidth) / NormalLength;
            const float Qx = static_cast<float>(WidthScale * NormalX);
            const float Qy = static_cast<float>(static_cast<double>(NormalY) * WidthScale);
            const float Qz = static_cast<float>(NormalZ * WidthScale);
            FAlDrawContext& Context = G_RectDrawContext;
            // Vertex stores are inlined below: skipped capacity paths do no coordinate arithmetic.
            unsigned short IndexA;
            const double VA = FrameBlock[2];
            const double UA = FrameBlock[3];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexA = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(P1.x) - Qx);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(P1.y) - Qy);
                Vertex->Z = static_cast<float>(static_cast<double>(P1.z) - Qz);
                Vertex->U = static_cast<float>(UA);
                Vertex->V = static_cast<float>(VA);
                Context.LockedVerts = Vertex + 1;
                IndexA = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexB;
            const double VB = FrameBlock[2];
            const double UB = FrameBlock[1];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexB = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(Qx) + P1.x);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(Qy) + P1.y);
                Vertex->Z = static_cast<float>(static_cast<double>(Qz) + P1.z);
                Vertex->U = static_cast<float>(UB);
                Vertex->V = static_cast<float>(VB);
                Context.LockedVerts = Vertex + 1;
                IndexB = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexC;
            const double VC = FrameBlock[4];
            const double UC = FrameBlock[3];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexC = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(P0.x) - Qx);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(P0.y) - Qy);
                Vertex->Z = static_cast<float>(static_cast<double>(P0.z) - Qz);
                Vertex->U = static_cast<float>(UC);
                Vertex->V = static_cast<float>(VC);
                Context.LockedVerts = Vertex + 1;
                IndexC = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexD;
            const double VD = FrameBlock[4];
            const double UD = FrameBlock[1];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexD = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(P0.x) + Qx);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(P0.y) + Qy);
                Vertex->Z = static_cast<float>(static_cast<double>(P0.z) + Qz);
                Vertex->U = static_cast<float>(UD);
                Vertex->V = static_cast<float>(VD);
                Context.LockedVerts = Vertex + 1;
                IndexD = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            StoreQuadIndices(Context, IndexA, IndexB, IndexC, IndexD);
        }
    }


#pragma float_control(pop)

    FxPerpAppearance::FxPerpAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kPerpClass)
    {
    }

    void FxPerpAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildPerpDescriptorRamps);
    }

    // FUN_06232a00: as basic's clone.
    GENRESULT DACOM_API FxPerpAppearance::CloneNode(IDAComponent** Out)
    {
        FxPerpAppearance* const Node = new (std::nothrow) FxPerpAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxPerpAppearance::~FxPerpAppearance()
    {
    }

    // Outer +0x14 -> FUN_06233040: the basic update body (FxBasicAppearance::UpdateNode).
    GENRESULT DACOM_API FxPerpAppearance::UpdateNode(float Dt)
    {
        G_PerpDrawContext.Field_0xA = 0;
        G_PerpDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & kAppFlagUseCommonTexFrame) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const int Handle = Texture_.Handle;
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                ITextureLibrary2* const Library = static_cast<ITextureLibrary2*>(G_TextureLibrary);
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                Library->GetFrame(Handle, static_cast<int>(Frame),
                    reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolPerp, Dt);
        return GR_OK;
    }

    // FUN_06232cc0: XYZ distance retains its unrounded squared length; the peeled
    // tail merge re-evaluates the head's age. Tail is captured after the first callback.
    GENRESULT DACOM_API FxPerpAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const double FirstRadius = ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime) * Scale;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const double TailRadius = ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const double Radius = ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::PerpUnroundedXyz);
            }
        }
        Vector WorldCenter;
        TransformPerpBoundsCenter(NodeWorldXform_,
            WorldCenter, *reinterpret_cast<const Vector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_06235db0: FUN_06234ab0 (FGenericFxObjectBase::
    // SpawnListParticle), then seed the new tail's axis at +0x30.
    // A velocity within DAT_0624eb68 (~1.0e-6, per squared component) of +X gets axis
    // (0, 0, 1); any other gets (1, 0, 0).
    GENRESULT DACOM_API FxPerpAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolPerp, Six, Age, Duration);
        const float Epsilon = PerpAxisParallelEpsilon();
        const double DeltaX = static_cast<double>(Six[3]) - 1.0f;
        const bool bParallel = DeltaX * DeltaX <= Epsilon &&
            static_cast<double>(Six[4]) * Six[4] <= Epsilon &&
            static_cast<double>(Six[5]) * Six[5] <= Epsilon;
        Vector* const Axis = reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(InstanceListTail_) + 0x30);
        if (bParallel)
        {
            Axis->x = 0.0f;
            Axis->y = 0.0f;
            Axis->z = 1.0f;
        }
        else
        {
            Axis->x = 1.0f;
            Axis->y = 0.0f;
            Axis->z = 0.0f;
        }
        return GR_OK;
    }

    // FUN_06233170 (node vtable 0x0624e8b0 +0x10; outer +0x18 thunk FUN_06235700): the
    // basic draw's shape, batch list 2, context DAT_06257aec, fill FUN_06233330. Returns 0
    // on every path.
    GENRESULT DACOM_API FxPerpAppearance::DrawOrEnqueue()
    {
        if (InstanceListHead_ == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(2, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_PerpDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        FillGeometry();

        // The rest is the same sequence as the outer +0xA0 body FUN_06235a60.
        EndBatchedDraw();
        return GR_OK;
    }

    // FUN_06235a40 (outer +0x9C): FUN_06240f50 on DAT_06257aec.
    GENRESULT DACOM_API FxPerpAppearance::BeginBatchedDraw(unsigned long Verts,
                                                          unsigned long Indices)
    {
        return AlDrawContextBegin(G_PerpDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // FUN_06235a60 (outer +0xA0): the fill wrote node-local vertices, so the modelview is
    // camera * node. The immediate draw FUN_06233170 runs the same sequence inline.
    GENRESULT DACOM_API FxPerpAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_PerpDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];

        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));

        void* FrameTexture = nullptr;
        std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
        G_RenderPipeline->SetTextureStageTexture(0, FrameTexture);

        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const float* const View = (const float*)Camera->GetViewTransform();
        Transform ViewWorld;
        Shared3DMath()->MulTransform(ViewWorld, *reinterpret_cast<const Transform*>(View),
                                   NodeWorldXform_);
        Transform Modelview;
        std::memcpy(&Modelview, &ViewWorld, sizeof(Modelview));
        G_RenderPipeline->SetModelview(&Modelview);

        AlDrawContextDraw(G_PerpDrawContext);
        return GR_OK;
    }

    // FUN_06235b40 (outer +0xA4).
    GENRESULT DACOM_API FxPerpAppearance::FillBatchedGeometry()
    {
        FillGeometry();
        return GR_OK;
    }

    // FUN_06233330: Perp basis, camera fade, atlas and four-vertex list walk.
#pragma float_control(except, on, push)
    void FxPerpAppearance::FillGeometry()
    {
        const float TinyLength = FloatFromBits(0x358637bdu);
        const float TinyAxisSquared = FloatFromBits(0x322bcc76u);
        const float FacingCull = FloatFromBits(0x3f7c1c5cu);
        const float FacingFade = FloatFromBits(0x3f5db3d7u);
        const float FacingFadeScale = FloatFromBits(0x4106b33du);

        ++G_DrawCounters[5];
        FPooledParticle* Particle = InstanceListHead_;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & kAppFlagUseCommonTexFrame) == 0;
        const bool bCameraFade = (Descriptor_->Flags & kAppFlagViewingAngleFade) != 0;
        Transform InvNode;
        float InverseW;
        ComputeGeneralInverseTransform(NodeWorldXform_,
                                       InvNode, InverseW);
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        if ((Descriptor_->Flags & kAppFlagFlipTexU) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
            FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
            std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
        }
        if ((Descriptor_->Flags & kAppFlagFlipTexV) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
            FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
            std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
        }
        std::int32_t LastFrame = -1;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        Vector CameraPosition;
        std::memcpy(&CameraPosition, Camera->GetPosition(), sizeof(CameraPosition));
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));

        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->Age) * Particle->InvDuration);
            float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const double UnroundedHalfSize = (ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale) * 0.5;
            const float HalfSize = static_cast<float>(UnroundedHalfSize);
            // Only an ordered less-or-equal skips; unordered continues.
            if (UnroundedHalfSize <= TinyLength || Alpha <= TinyLength)
                continue;

            float* const Axis = reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(Particle) + 0x30);
            Vector Velocity;
            std::memcpy(&Velocity, &Particle->VelX, sizeof(Velocity));
            Vector PreviousAxis;
            std::memcpy(&PreviousAxis, Axis, sizeof(PreviousAxis));
            const double VelocitySquared = ((static_cast<double>(Velocity.x) * Velocity.x) +
                (static_cast<double>(Velocity.z) * Velocity.z)) + (static_cast<double>(Velocity.y) * Velocity.y);
            const float RoundedVelocitySquared = static_cast<float>(VelocitySquared);
            std::int32_t VelocityBits;
            std::memcpy(&VelocityBits, &RoundedVelocitySquared, sizeof(VelocityBits));
            const std::uint32_t VelocityGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(VelocityBits >> 1);
            float VelocityGuess;
            std::memcpy(&VelocityGuess, &VelocityGuessBits, sizeof(VelocityGuess));
            const double VelocityInverse = (1.5 - (((static_cast<double>(VelocityGuess) * VelocityGuess) *
                VelocitySquared) * 0.5)) * VelocityGuess;
            const double VelocityLength = VelocitySquared * VelocityInverse;
            if (!(VelocityLength <= TinyLength))
            {
                const double InverseLength = 1.0 / VelocityLength;
                Velocity.x = static_cast<float>(static_cast<double>(Velocity.x) * InverseLength);
                Velocity.y = static_cast<float>(static_cast<double>(Velocity.y) * InverseLength);
                Velocity.z = static_cast<float>(static_cast<double>(Velocity.z) * InverseLength);
            }
            else
            {
                Velocity.x = 0.0f;
                Velocity.y = 1.0f;
                Velocity.z = 0.0f;
                const double DifferenceX = -static_cast<double>(PreviousAxis.x);
                const double DifferenceY = 1.0 - PreviousAxis.y;
                const double DifferenceZ = -static_cast<double>(PreviousAxis.z);
                if (DifferenceX * DifferenceX <= TinyAxisSquared &&
                    DifferenceY * DifferenceY <= TinyAxisSquared &&
                    DifferenceZ * DifferenceZ <= TinyAxisSquared)
                {
                    Velocity.x = 1.0f;
                    Velocity.y = 0.0f;
                }
            }
            Vector Position;
            std::memcpy(&Position, &Particle->PosX, sizeof(Position));
            if (bCameraFade)
            {
                Vector LocalCamera;
                Shared3DMath()->TransformXform(LocalCamera, InvNode, CameraPosition);
                Vector ToCamera;
                ToCamera.x = static_cast<float>(static_cast<double>(LocalCamera.x) - Position.x);
                ToCamera.y = static_cast<float>(static_cast<double>(LocalCamera.y) - Position.y);
                ToCamera.z = static_cast<float>(static_cast<double>(LocalCamera.z) - Position.z);
                const double CameraSquared = ((static_cast<double>(ToCamera.x) * ToCamera.x) +
                    (static_cast<double>(ToCamera.z) * ToCamera.z)) + (static_cast<double>(ToCamera.y) * ToCamera.y);
                const float RoundedCameraSquared = static_cast<float>(CameraSquared);
                std::int32_t CameraBits;
                std::memcpy(&CameraBits, &RoundedCameraSquared, sizeof(CameraBits));
                const std::uint32_t CameraGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(CameraBits >> 1);
                float CameraGuess;
                std::memcpy(&CameraGuess, &CameraGuessBits, sizeof(CameraGuess));
                const double CameraInverse = (1.5 - (((static_cast<double>(CameraGuess) * CameraGuess) *
                    CameraSquared) * 0.5)) * CameraGuess;
                const double CameraLength = CameraSquared * CameraInverse;
                if (!(CameraLength <= TinyLength))
                {
                    const double InverseLength = 1.0 / CameraLength;
                    ToCamera.x = static_cast<float>(static_cast<double>(ToCamera.x) * InverseLength);
                    ToCamera.y = static_cast<float>(static_cast<double>(ToCamera.y) * InverseLength);
                    ToCamera.z = static_cast<float>(static_cast<double>(ToCamera.z) * InverseLength);
                }
                else
                {
                    ToCamera.x = 0.0f;
                    ToCamera.y = 1.0f;
                    ToCamera.z = 0.0f;
                }
                const double Dot = ((static_cast<double>(ToCamera.x) * Velocity.x) +
                    (static_cast<double>(ToCamera.z) * Velocity.z)) + (static_cast<double>(ToCamera.y) * Velocity.y);
                const double UnroundedFacing = 1.0 - std::fabs(Dot);
                const float Facing = static_cast<float>(UnroundedFacing);
                // Only an ordered greater skips, tested on the unrounded value.
                if (UnroundedFacing > FacingCull)
                    continue;
                if (Facing > FacingFade)
                {
                    const double Fade = (static_cast<double>(FacingCull) - Facing) * FacingFadeScale;
                    Alpha = static_cast<float>((static_cast<double>(Alpha) * Fade) * Fade);
                }
                if (Alpha <= TinyLength)
                    continue;
            }
            const unsigned long PackedColor = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0f);
            const unsigned long Diffuse = PackedColor | (static_cast<unsigned long>(AlphaInteger) << 24);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime);
                const std::int32_t FrameCountMinusOne = static_cast<std::int32_t>(Texture_.FrameCount - 1u);
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * FrameCountMinusOne);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    if ((Descriptor_->Flags & kAppFlagFlipTexU) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[1], sizeof(Previous));
                        FrameBlock[1] = static_cast<float>(LoadAppearanceScalar(FrameBlock[3]));
                        std::memcpy(&FrameBlock[3], &Previous, sizeof(Previous));
                    }
                    if ((Descriptor_->Flags & kAppFlagFlipTexV) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[2], sizeof(Previous));
                        FrameBlock[2] = static_cast<float>(LoadAppearanceScalar(FrameBlock[4]));
                        std::memcpy(&FrameBlock[4], &Previous, sizeof(Previous));
                    }
                }
            }
            const float NormalX = static_cast<float>((static_cast<double>(Velocity.z) * PreviousAxis.y) -
                (static_cast<double>(Velocity.y) * PreviousAxis.z));
            const float NormalY = static_cast<float>((static_cast<double>(PreviousAxis.z) * Velocity.x) -
                (static_cast<double>(Velocity.z) * PreviousAxis.x));
            const float NormalZ = static_cast<float>((static_cast<double>(Velocity.y) * PreviousAxis.x) -
                (static_cast<double>(PreviousAxis.y) * Velocity.x));
            const float NormalSquared = static_cast<float>(((static_cast<double>(NormalX) * NormalX) +
                (static_cast<double>(NormalZ) * NormalZ)) + (static_cast<double>(NormalY) * NormalY));
            std::int32_t NormalBits;
            std::memcpy(&NormalBits, &NormalSquared, sizeof(NormalBits));
            const std::uint32_t NormalGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(NormalBits >> 1);
            float NormalGuess;
            std::memcpy(&NormalGuess, &NormalGuessBits, sizeof(NormalGuess));
            const double NormalInverse = (1.5 - (((static_cast<double>(NormalGuess) * NormalGuess) *
                NormalSquared) * 0.5)) * NormalGuess;
            const double NormalizedX = static_cast<double>(NormalX) * NormalInverse;
            const float NormalizedY = static_cast<float>(static_cast<double>(NormalY) * NormalInverse);
            const double UnroundedNormalizedZ = static_cast<double>(NormalZ) * NormalInverse;
            const float NormalizedZ = static_cast<float>(UnroundedNormalizedZ);
            Vector NewAxis;
            NewAxis.x = static_cast<float>((UnroundedNormalizedZ * Velocity.y) -
                (static_cast<double>(NormalizedY) * Velocity.z));
            NewAxis.y = static_cast<float>((static_cast<double>(Velocity.z) * NormalizedX) -
                (static_cast<double>(NormalizedZ) * Velocity.x));
            NewAxis.z = static_cast<float>((static_cast<double>(NormalizedY) * Velocity.x) -
                (static_cast<double>(Velocity.y) * NormalizedX));
            std::memcpy(Axis, &NewAxis, sizeof(NewAxis));
            const float ScaledNormalX = static_cast<float>(NormalizedX * HalfSize);
            const float ScaledNormalY = static_cast<float>(static_cast<double>(NormalizedY) * HalfSize);
            const float ScaledNormalZ = static_cast<float>(static_cast<double>(NormalizedZ) * HalfSize);
            const float ScaledAxisX = static_cast<float>(static_cast<double>(NewAxis.x) * HalfSize);
            const float ScaledAxisY = static_cast<float>(static_cast<double>(NewAxis.y) * HalfSize);
            const double UnroundedScaledAxisZ = static_cast<double>(NewAxis.z) * HalfSize;
            const float ScaledAxisZ = static_cast<float>(UnroundedScaledAxisZ);
            const float SumX = static_cast<float>(static_cast<double>(ScaledAxisX) + ScaledNormalX);
            const float SumY = static_cast<float>(static_cast<double>(ScaledAxisY) + ScaledNormalY);
            const float SumZ = static_cast<float>(UnroundedScaledAxisZ + ScaledNormalZ);
            const double DifferenceX = static_cast<double>(ScaledAxisX) - ScaledNormalX;
            const double DifferenceY = static_cast<double>(ScaledAxisY) - ScaledNormalY;
            const double DifferenceZ = static_cast<double>(ScaledAxisZ) - ScaledNormalZ;
            FAlDrawContext& Context = G_PerpDrawContext;
            const double V0 = FrameBlock[2];
            const double U1 = FrameBlock[3];
            unsigned short IndexA;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexA = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->Diffuse = Diffuse;
                Vertex->X = static_cast<float>(DifferenceX + Position.x);
                Vertex->Y = static_cast<float>(DifferenceY + Position.y);
                Vertex->Z = static_cast<float>(DifferenceZ + Position.z);
                Vertex->U = static_cast<float>(U1);
                Vertex->V = static_cast<float>(V0);
                Context.LockedVerts = Vertex + 1;
                IndexA = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            const double VertexBY = static_cast<double>(Position.y) - SumY;
            const float VertexBZ = static_cast<float>(static_cast<double>(Position.z) - SumZ);
            unsigned short IndexB;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexB = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                std::memcpy(&Vertex->Z, &VertexBZ, sizeof(VertexBZ));
                std::memcpy(&Vertex->U, &FrameBlock[1], sizeof(Vertex->U));
                Vertex->X = static_cast<float>(static_cast<double>(Position.x) - SumX);
                Vertex->Diffuse = Diffuse;
                std::memcpy(&Vertex->V, &FrameBlock[2], sizeof(Vertex->V));
                Vertex->Y = static_cast<float>(VertexBY);
                Context.LockedVerts = Vertex + 1;
                IndexB = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            const double VertexCY = static_cast<double>(SumY) + Position.y;
            const float VertexCZ = static_cast<float>(static_cast<double>(SumZ) + Position.z);
            unsigned short IndexC;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexC = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                std::memcpy(&Vertex->Z, &VertexCZ, sizeof(VertexCZ));
                std::memcpy(&Vertex->U, &FrameBlock[3], sizeof(Vertex->U));
                Vertex->X = static_cast<float>(static_cast<double>(SumX) + Position.x);
                Vertex->Diffuse = Diffuse;
                std::memcpy(&Vertex->V, &FrameBlock[4], sizeof(Vertex->V));
                Vertex->Y = static_cast<float>(VertexCY);
                Context.LockedVerts = Vertex + 1;
                IndexC = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexD;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexD = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex Vertex;
                Vertex.V = FrameBlock[4];
                Vertex.U = FrameBlock[1];
                Vertex.Diffuse = Diffuse;
                Vertex.Z = static_cast<float>(static_cast<double>(Position.z) - DifferenceZ);
                Vertex.Y = static_cast<float>(static_cast<double>(Position.y) - DifferenceY);
                Vertex.X = static_cast<float>(static_cast<double>(Position.x) - DifferenceX);
                FAlchemyVertex* const Out = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                std::memcpy(Out, &Vertex, sizeof(Vertex));
                Context.LockedVerts = Out + 1;
                IndexD = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            StoreQuadIndices(Context, IndexA, IndexB, IndexC, IndexD);

        }
    }
#pragma float_control(pop)

    FxOrientedAppearance::FxOrientedAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kOrientedClass)
    {
    }

    void FxOrientedAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildOrientedDescriptorRamps);
    }

    // LAB_0622e950: four vertices per node-local particle.
    GENRESULT DACOM_API FxOrientedAppearance::GetQueuedVertCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ << 2;
        return GR_OK;
    }

    // LAB_0622e970: six indices per node-local particle.
    GENRESULT DACOM_API FxOrientedAppearance::GetQueuedIndexCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ * 6U;
        return GR_OK;
    }

    // FUN_0622bbc0: as basic's clone.
    GENRESULT DACOM_API FxOrientedAppearance::CloneNode(IDAComponent** Out)
    {
        FxOrientedAppearance* const Node = new (std::nothrow) FxOrientedAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        // Retail reloads the prototype cache after initialization callbacks.
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxOrientedAppearance::~FxOrientedAppearance()
    {
    }

    // Outer +0x14 -> FUN_0622c3d0: the basic update body (FxBasicAppearance::UpdateNode).
    GENRESULT DACOM_API FxOrientedAppearance::UpdateNode(float Dt)
    {
        G_OrientedDrawContext.Field_0xA = 0;
        G_OrientedDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & kAppFlagUseCommonTexFrame) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->CommonTexFrameRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                static_cast<ITextureLibrary2*>(G_TextureLibrary)->GetFrame(Texture_.Handle,
                    static_cast<int>(Frame), reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolOriented, Dt);
        return GR_OK;
    }

    // Outer +0x8C -> FUN_0622eca0 -> FUN_0622bed0: rect's FUN_06238280 with the oriented
    // max (C > B ? C : B; differs from rect only for NaN), the peeled tail pass at t0 and
    // math engine DAT_06257ac0.
    GENRESULT DACOM_API FxOrientedAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        float FirstOuterTime;
        std::memcpy(&FirstOuterTime, &AppearanceOuterTime_, sizeof(float));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const float FirstPrimary = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, FirstOuterTime, FirstInnerTime) * Scale);
        const float FirstB = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->TexFrameRamp, AppearanceOuterTime_, FirstInnerTime));
        const double FirstC = ForwardAppearanceSingleEval(
            Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, FirstInnerTime);
        const double FirstSide = FirstC > static_cast<double>(FirstB) ? FirstC : FirstB;
        const double FirstRadius = FirstSide * FirstPrimary;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const float TailPrimary = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale);
            const float TailB = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, TailInnerTime));
            const double TailC = ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, TailInnerTime);
            const double TailSide = TailC > static_cast<double>(TailB) ? TailC : TailB;
            const double TailRadius = TailSide * TailPrimary;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::RectRoundedYzx);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const float Primary = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
                const float B = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
                const double C = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const double Side = C > static_cast<double>(B) ? C : B;
                const double Radius = Side * Primary;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::RectRoundedYzx);
            }
        }
        Vector WorldCenter;
        TransformOrientedBoundsCenter(NodeWorldXform_,
            WorldCenter, *reinterpret_cast<const Vector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_0622ee90 -> FUN_0622bd80. See
    // FGenericFxObjectBase::SpawnListParticle.
    GENRESULT DACOM_API FxOrientedAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolOriented, Six, Age, Duration);
        return GR_OK;
    }

    // FUN_0622c500, __stdcall on the node: the draw and fill in one, with no batcher
    // branch; G_DrawCounters[6] is bumped before the empty test. Particles with Alpha == 0
    // or an ordered size < 1e-11 (0x2d2febff) are skipped. The per-particle frame comes
    // from LocalTransform (+0x8c, unchecked as in retail). Camera fade (flag 0x40):
    // F = 1 - |N . D| with N = col0 x col1 of the frame and D the unit vector to the
    // camera (length summed x, z, y; |D| <= 1e-6 gives (0,1,0)); F > 0.98481 skips,
    // F > 0.866025 scales Alpha by ((0.98481 - F) * 8.41883)^2. The last vertex goes
    // through FUN_06208a40.
    #pragma float_control(except, on, push)
    GENRESULT DACOM_API FxOrientedAppearance::DrawOrEnqueue()
    {
        const float TinyScale = FloatFromBits(0x2d2febffu);
        const float TinyLength = FloatFromBits(0x358637bdu);
        const float FacingCull = FloatFromBits(0x3f7c1c5cu);
        const float FacingFade = FloatFromBits(0x3f5db3d7u);
        const float FacingFadeScale = FloatFromBits(0x4106b33du);
        ++G_DrawCounters[6];
        const FPooledParticle* Particle = InstanceListHead_;
        if (Particle == nullptr)
            return GR_OK;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & kAppFlagUseCommonTexFrame) == 0;
        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_OrientedDrawContext, 4, VertCount, IndexCount))
            return GR_OK;
        const bool bCameraFade = (Descriptor_->Flags & kAppFlagOrientedViewingAngleFade) != 0;
        Transform InverseNode;
        Vector CameraPosition;
        if (bCameraFade)
        {
            Transform InverseTemporary;
            float InverseW;
            ComputeGeneralInverseTransform(NodeWorldXform_,
                InverseTemporary, InverseW);
            std::memcpy(&InverseNode, &InverseTemporary, sizeof(InverseNode));
            ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
            std::memcpy(&CameraPosition, Camera->GetPosition(), sizeof(CameraPosition));
        }
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        const FAppearanceRampDescriptor* const InitialDescriptor = Descriptor_;
        FlipBasicAppearanceFrame(InitialDescriptor->Flags, FrameBlock);
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            InitialDescriptor->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        std::int32_t LastFrame = -1;
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            const float Height = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime) * SizeScale);
            const float Width = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime) * SizeScale);
            if (!IsNonzeroParticleField(Alpha))
                continue;
            if (SizeScale < TinyScale)
                continue;
            float Position[3];
            std::memcpy(Position, &Particle->PosX, sizeof(Position));
            Transform M;
            if (static_cast<unsigned char>(Descriptor_->LocalTransform->IsIdentity()) == 0)
            {
                unsigned long OuterBits;
                unsigned long InnerBits;
                std::memcpy(&OuterBits, &AppearanceOuterTime_, sizeof(OuterBits));
                std::memcpy(&InnerBits, &InnerTime, sizeof(InnerBits));
                Descriptor_->LocalTransform->Evaluate(OuterBits, InnerBits, &M);
                Position[0] = static_cast<float>(static_cast<double>(M.Translation.x) + Position[0]);
                Position[1] = static_cast<float>(static_cast<double>(Position[1]) + M.Translation.y);
                Position[2] = static_cast<float>(static_cast<double>(Position[2]) + M.Translation.z);
            }
            else
            {
                FillIdentity3x3(&M.Rotation);
                M.Translation.z = 0.0f;
                M.Translation.y = 0.0f;
                M.Translation.x = 0.0f;
            }
            if (bCameraFade)
            {
                const double B1 = LoadAppearanceScalar(M.Rotation.E[0][1]);
                const double B4 = LoadAppearanceScalar(M.Rotation.E[1][1]);
                const double B7 = LoadAppearanceScalar(M.Rotation.E[2][1]);
                const float A0 = static_cast<float>(LoadAppearanceScalar(M.Rotation.E[0][0]));
                const float A3 = static_cast<float>(LoadAppearanceScalar(M.Rotation.E[1][0]));
                const double A6 = LoadAppearanceScalar(M.Rotation.E[2][0]);
                const float NormalX = static_cast<float>(static_cast<double>(A3) * B7 - A6 * B4);
                const float NormalY = static_cast<float>(A6 * B1 - B7 * A0);
                const float NormalZ = static_cast<float>(B4 * A0 - static_cast<double>(A3) * B1);
                Vector LocalCamera;
                TransformOrientedBoundsCenter(InverseNode, LocalCamera, CameraPosition);
                float Direction[3];
                Direction[2] = static_cast<float>(static_cast<double>(LocalCamera.z) - Position[2]);
                Direction[1] = static_cast<float>(static_cast<double>(LocalCamera.y) - Position[1]);
                Direction[0] = static_cast<float>(static_cast<double>(LocalCamera.x) - Position[0]);
                const double SquareX = static_cast<double>(Direction[0]) * Direction[0];
                const double SquareZ = static_cast<double>(Direction[2]) * Direction[2];
                const double SumXz = SquareX + SquareZ;
                const double SquareY = static_cast<double>(Direction[1]) * Direction[1];
                const double LengthSquared = SumXz + SquareY;
                const float SeedInput = static_cast<float>(LengthSquared);
                std::int32_t LengthBits;
                std::memcpy(&LengthBits, &SeedInput, sizeof(LengthBits));
                const std::uint32_t GuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(LengthBits >> 1);
                float Guess;
                std::memcpy(&Guess, &GuessBits, sizeof(Guess));
                const double InverseLength = (1.5 - (((static_cast<double>(Guess) * Guess) * LengthSquared) * 0.5)) * Guess;
                const double Length = InverseLength * LengthSquared;
                if (!(Length <= TinyLength))
                {
                    const double InverseDistance = 1.0 / Length;
                    Direction[0] = static_cast<float>(static_cast<double>(Direction[0]) * InverseDistance);
                    Direction[1] = static_cast<float>(static_cast<double>(Direction[1]) * InverseDistance);
                    Direction[2] = static_cast<float>(static_cast<double>(Direction[2]) * InverseDistance);
                }
                else
                {
                    Direction[0] = 0.0f;
                    Direction[1] = 1.0f;
                    Direction[2] = 0.0f;
                }
                const double DotX = static_cast<double>(NormalX) * Direction[0];
                const double DotZ = static_cast<double>(Direction[2]) * NormalZ;
                const double SumXzDot = DotX + DotZ;
                const double DotY = static_cast<double>(Direction[1]) * NormalY;
                const double FacingValue = 1.0 - std::fabs(SumXzDot + DotY);
                const float Facing = static_cast<float>(FacingValue);
                if (FacingValue > FacingCull)
                    continue;
                if (Facing > FacingFade)
                {
                    const double Fade = (static_cast<double>(FacingCull) - Facing) * FacingFadeScale;
                    Alpha = static_cast<float>((static_cast<double>(Alpha) * Fade) * Fade);
                }
                if (!IsNonzeroParticleField(Alpha))
                    continue;
            }
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->FrameRamp, AppearanceOuterTime_, InnerTime);
                const std::uint32_t CountBits = Texture_.FrameCount - 1u;
                std::int32_t SignedCount;
                std::memcpy(&SignedCount, &CountBits, sizeof(SignedCount));
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * SignedCount);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    const FAppearanceRampDescriptor* const FrameDescriptor = Descriptor_;
                    if ((FrameDescriptor->Flags & kAppFlagFlipTexU) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[1], sizeof(Previous));
                        FrameBlock[1] = static_cast<float>(LoadAppearanceScalar(FrameBlock[3]));
                        std::memcpy(&FrameBlock[3], &Previous, sizeof(Previous));
                    }
                    if ((FrameDescriptor->Flags & kAppFlagFlipTexV) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[2], sizeof(Previous));
                        FrameBlock[2] = static_cast<float>(LoadAppearanceScalar(FrameBlock[4]));
                        std::memcpy(&FrameBlock[4], &Previous, sizeof(Previous));
                    }
                }
            }
            float A[3];
            float B[3];
            A[2] = static_cast<float>(static_cast<double>(M.Rotation.E[2][0]) * Width);
            A[1] = static_cast<float>(static_cast<double>(M.Rotation.E[1][0]) * Width);
            A[0] = static_cast<float>(static_cast<double>(M.Rotation.E[0][0]) * Width);
            B[2] = static_cast<float>(static_cast<double>(M.Rotation.E[2][1]) * Height);
            B[1] = static_cast<float>(static_cast<double>(M.Rotation.E[1][1]) * Height);
            B[0] = static_cast<float>(static_cast<double>(M.Rotation.E[0][1]) * Height);
            float MinusA[3];
            float PlusA[3];
            float Corners[4][3];
            for (int Axis = 2; Axis >= 0; --Axis)
                MinusA[Axis] = static_cast<float>(static_cast<double>(Position[Axis]) - A[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[3][Axis] = static_cast<float>(static_cast<double>(MinusA[Axis]) - B[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                PlusA[Axis] = static_cast<float>(static_cast<double>(A[Axis]) + Position[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[2][Axis] = static_cast<float>(static_cast<double>(PlusA[Axis]) - B[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                MinusA[Axis] = static_cast<float>(static_cast<double>(Position[Axis]) - A[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[1][Axis] = static_cast<float>(static_cast<double>(MinusA[Axis]) + B[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                PlusA[Axis] = static_cast<float>(static_cast<double>(A[Axis]) + Position[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[0][Axis] = static_cast<float>(static_cast<double>(PlusA[Axis]) + B[Axis]);
            unsigned short Indices[4];
            for (unsigned long Corner = 0; Corner < 4; ++Corner)
            {
                double Y = 0.0;
                double Z = 0.0;
                double U = 0.0;
                double V = 0.0;
                if (Corner == 0)
                {
                    V = LoadAppearanceScalar(FrameBlock[2]);
                    U = LoadAppearanceScalar(FrameBlock[3]);
                    Z = LoadAppearanceScalar(Corners[0][2]);
                    Y = LoadAppearanceScalar(Corners[0][1]);
                }
                else if (Corner != 3)
                    Y = LoadAppearanceScalar(Corners[Corner][1]);
                const long Count = G_OrientedDrawContext.VertCount;
                Indices[Corner] = static_cast<unsigned short>(G_OrientedDrawContext.RequestedVerts);
                if (Count > static_cast<long>(G_OrientedDrawContext.RequestedVerts))
                    continue;
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_OrientedDrawContext.LockedVerts);
                if (Corner == 0)
                {
                    Vertex->X = static_cast<float>(LoadAppearanceScalar(Corners[0][0]));
                    Vertex->Diffuse = Diffuse;
                    Vertex->Y = static_cast<float>(Y);
                    Vertex->Z = static_cast<float>(Z);
                    Vertex->U = static_cast<float>(U);
                    Vertex->V = static_cast<float>(V);
                }
                else if (Corner != 3)
                {
                    std::memcpy(&Vertex->Z, &Corners[Corner][2], sizeof(float));
                    Vertex->X = static_cast<float>(LoadAppearanceScalar(Corners[Corner][0]));
                    std::memcpy(&Vertex->U, &FrameBlock[Corner == 1 ? 1 : 3], sizeof(float));
                    Vertex->Y = static_cast<float>(Y);
                    Vertex->Diffuse = Diffuse;
                    std::memcpy(&Vertex->V, &FrameBlock[Corner == 1 ? 2 : 4], sizeof(float));
                }
                else
                {
                    float Values[5];
                    Values[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[4]));
                    Values[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
                    Values[2] = static_cast<float>(LoadAppearanceScalar(Corners[3][2]));
                    Values[1] = static_cast<float>(LoadAppearanceScalar(Corners[3][1]));
                    Values[0] = static_cast<float>(LoadAppearanceScalar(Corners[3][0]));
                    std::memcpy(&Vertex->X, &Values[0], sizeof(float));
                    std::memcpy(&Vertex->Y, &Values[1], sizeof(float));
                    std::memcpy(&Vertex->Z, &Values[2], sizeof(float));
                    Vertex->Diffuse = Diffuse;
                    std::memcpy(&Vertex->U, &Values[3], sizeof(float));
                    std::memcpy(&Vertex->V, &Values[4], sizeof(float));
                }
                Indices[Corner] = static_cast<unsigned short>(Count);
                G_OrientedDrawContext.LockedVerts = Vertex + 1;
                ++G_OrientedDrawContext.VertCount;
            }
            StoreQuadIndices(G_OrientedDrawContext, Indices[0], Indices[1], Indices[2], Indices[3]);
        }
        if (AlDrawContextEnd(G_OrientedDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];
        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));
        void* FrameTexture;
        std::memcpy(&FrameTexture, Texture_.Frame, sizeof(FrameTexture));
        G_RenderPipeline->SetTextureStageTexture(0, FrameTexture);
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        Transform ViewWorld;
        ComposeTransform(&ViewWorld, Camera->GetViewTransform(), NodeWorldXform_);
        G_RenderPipeline->SetModelview(&ViewWorld);
        AlDrawContextDraw(G_OrientedDrawContext);
        return GR_OK;
    }
    #pragma float_control(pop)
    // Node ctor FUN_0620f4e0: +0xec identity (1.0 at +0xec / +0xfc / +0x10c, the rest 0),
    // +0x11c = 0.05 (0x3d4ccccd), +0x120..+0x12c = 0.
    FLDustAppearance::FLDustAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kDustClass),
          LastDt_(FloatFromBits(0x3d4ccccdu)), LastCameraStep_(0.0f)
    {
        FillIdentity3x3(&ViewXform_.Rotation);
        ViewXform_.Translation.x = 0.0f;
        ViewXform_.Translation.y = 0.0f;
        ViewXform_.Translation.z = 0.0f;
        LastCameraPos_.x = 0.0f;
        LastCameraPos_.y = 0.0f;
        LastCameraPos_.z = 0.0f;
    }

    // FUN_0620f6b0: the set FUN_0620ef80 (perp's builder, cached at node+0xe8).
    void FLDustAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildPerpDescriptorRamps);
    }

    // FUN_062125e0: outer +0xf0 is the node's own live-instance count.
    GENRESULT DACOM_API FLDustAppearance::GetQueuedVertCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ << 2;
        return GR_OK;
    }

    // FUN_06212600: the same count contributes six indices per dust particle.
    GENRESULT DACOM_API FLDustAppearance::GetQueuedIndexCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ * 6U;
        return GR_OK;
    }

    // FUN_0620f600: as basic's clone, with the set cache at +0xe8.
    GENRESULT DACOM_API FLDustAppearance::CloneNode(IDAComponent** Out)
    {
        FLDustAppearance* const Node = new (std::nothrow) FLDustAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_0620fb70, reached through outer +0x14 FUN_06212220: the basic update with Dt also
    // stored at node+0x11c; field walk FUN_062114c0, integrate FUN_06211600 (Age <= Duration
    // or NaN keeps a particle).
#pragma float_control(except, on, push)
    GENRESULT DACOM_API FLDustAppearance::UpdateNode(float Dt)
    {
        // The initial retail FLD precedes both counter stores. A float is exact
        // in double; this native spill keeps the load and later float store apart.
        volatile double PendingDt = static_cast<double>(Dt);
        volatile unsigned short& VertCounter = G_DustDrawContext.Field_0xA;
        volatile unsigned short& IndexCounter = G_DustDrawContext.PrimCount;
        VertCounter = 0;
        IndexCounter = 0;
        const double StoredDt = PendingDt;
        StoreAlchemyFpuFloat(LastDt_, StoredDt);
        const double NextAge = static_cast<double>(Dt) + Age_;
        StoreAlchemyFpuFloat(Age_, NextAge);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;

        FFxTextureKey Key;
        Key.Name = kTexNameUnknown;
        Key.Crc = Descriptor_->TextureCrc;
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & kAppFlagUseCommonTexFrame) == kAppFlagUseCommonTexFrame)
        {
            // Texture callbacks can publish a different live descriptor.
            const double PositionValue = ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, Age_);
            float Position;
            StoreAlchemyFpuFloat(Position, PositionValue);
            const int FrameCount = Texture_.FrameCount;
            if (FrameCount != 1)
            {
                const int Handle = Texture_.Handle;
                if (Handle != -1)
                {
                    const unsigned long Span = static_cast<unsigned long>(FrameCount) - 1U;
                    const std::int64_t SpanWide = static_cast<std::int64_t>(Span);
                    const double WideSpan = ComputeAlchemyFpuWideInteger(SpanWide);
                    IDAComponent* const volatile& LibrarySlot = G_TextureLibrary;
                    ITextureLibrary2* const Library = static_cast<ITextureLibrary2*>(LibrarySlot);
                    const std::int64_t Frame = ConvertAlchemyFpuInteger(WideSpan * Position);
                    Library->GetFrame(Handle, static_cast<int>(Frame),
                                      reinterpret_cast<unsigned long*>(Texture_.Frame));
                }
            }
        }
        else if (Texture_.FrameCount != 1)
        {
            Texture_.Advance(Dt);
        }
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolDust, Dt);
        return GR_OK;
    }
#pragma float_control(pop)

    // FUN_0620f830: the basic bounds body (FxBasicAppearance::GetBoundingSphere) with no
    // Appearance_LODCurve scale, the radius being the +0x78 ramp alone. As in perp, the
    // peeled tail merge evaluates the head's t (retail).
    GENRESULT DACOM_API FLDustAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const double FirstRadius = ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime);
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const double TailRadius = ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime);
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const double Radius = ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime);
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::PerpUnroundedXyz);
            }
        }
        Vector WorldCenter;
        TransformPerpBoundsCenter(NodeWorldXform_,
            WorldCenter, *reinterpret_cast<const Vector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // FUN_06212700: a full pool (DAT_062579b4 == 0) first evicts the oldest particle
    // (FUN_062121d0); then FUN_06211440 spawns, and the tail's +0x30 gets the spawn point
    // in camera space (node+0xec). A failed allocation stores through null in retail and
    // does here too.
    GENRESULT DACOM_API FLDustAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        if (G_PoolDust.FreeCount == 0)
            EvictOldestListParticle(&G_PoolDust);

        FPooledParticle* Particle = static_cast<FPooledParticle*>(AllocPoolItem(&G_PoolDust));
        if (Particle != nullptr)
        {
            Particle->PosX = Six[0];
            Particle->PosY = Six[1];
            Particle->PosZ = Six[2];
            Particle->VelX = Six[3];
            Particle->VelY = Six[4];
            Particle->VelZ = Six[5];
            Particle->Appearance = this;
            Particle->Age = Age;
            Particle->Duration = Duration;
            Particle->InvDuration = 1.0f / Duration;
            ++LiveInstanceCount_;
        }
        AppendParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);

        float* LastViewPos =
            reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(InstanceListTail_) + 0x30);
        CameraSpacePoint(&ViewXform_, Six, LastViewPos);
        return GR_OK;
    }

    // FUN_0620fcb0, reached through outer +0x18 FUN_06212240: draw and fill in one; the
    // camera-space ViewWorld is kept at node+0xec. Fade by how far the camera moved since
    // the last draw, per unit of Dt (node+0x11c): v < 100 (ordered) 1 - (1 - v/100)^2,
    // v < 1200 1 - ((v - 100)/1100)^2, else or unordered 0; a change of more than 20
    // against the last step (node+0x12c) fades out by 35 (.data DAT_062559fc / 06255a00 /
    // 06255a04 / 06255a08, read-only). Streaks run from the particle's previous camera
    // position (P+0x30, updated for every particle, drawn or not); DAT_062579cc, a .data
    // byte 0 with one reader, would switch to the camera step. The tail stores the camera
    // position (node+0x120) and step length (node+0x12c) even when nothing was drawn.
    namespace
    {
    }

#pragma optimize("", off)
#pragma float_control(except, on, push)
#pragma intrinsic(sqrt)
    GENRESULT DACOM_API FLDustAppearance::DrawOrEnqueue()
    {
        const float kFadeNear = FloatFromBits(0x42c80000u);    // 100
        const float kFadeFar = FloatFromBits(0x44960000u);     // 1200
        const float kJerkLow = FloatFromBits(0x41a00000u);     // 20
        const float kJerkHigh = FloatFromBits(0x420c0000u);    // 35
        const float kStreakScale = FloatFromBits(0x3d0f5c29u); // 0.035
        const float kStreakMax = FloatFromBits(0x42c80000u);   // 100
        const float kTailScale = FloatFromBits(0x3f266666u);   // 0.65
        const float kHeadScale = FloatFromBits(0x3fc4ec4fu);   // 1.53846
        const float kMinAlpha = FloatFromBits(0x3c23d70au);    // 0.01

        ++G_DrawCounters[8];
        FPooledParticle* Particle = InstanceListHead_;
        if (Particle == nullptr)
            return GR_OK;

        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const Transform* CameraView = Camera->GetViewTransform();
        ComposeTransform(&ViewXform_, CameraView, NodeWorldXform_);

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);

        Vector CameraPos;
        ICamera* const PositionCamera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        std::memcpy(&CameraPos, PositionCamera->GetPosition(), sizeof(CameraPos));
        Vector CameraDelta;
        StoreAlchemyFpuFloat(CameraDelta.x, static_cast<double>(CameraPos.x) - LastCameraPos_.x);
        StoreAlchemyFpuFloat(CameraDelta.y, static_cast<double>(CameraPos.y) - LastCameraPos_.y);
        StoreAlchemyFpuFloat(CameraDelta.z, static_cast<double>(CameraPos.z) - LastCameraPos_.z);
        Vector Step;
        Shared3DMath()->TransformMatrix(Step,
            CameraView->Rotation,
            CameraDelta);
        const double SquareX = static_cast<double>(Step.x) * Step.x;
        const double SquareY = static_cast<double>(Step.y) * Step.y;
        const double SumXy = SquareX + SquareY;
        const double SquareZ = static_cast<double>(Step.z) * Step.z;
        const double LengthSquared = SumXy + SquareZ;
        const double Length = ComputeAlchemyFpuSquareRoot(LengthSquared);
        float StepLen;
        StoreAlchemyFpuFloat(StepLen, (Length));
        // Kept on the FPU stack in retail (the length is stored, the quotient never
        // spilled), hence double here.
        const double Speed = Length / LastDt_;
        float Fade = 0.0f;
        if (Speed < kFadeNear)
        {
            const double Rise = 1.0 - Speed / kFadeNear;
            StoreAlchemyFpuFloat(Fade, (1.0 - Rise * Rise));
        }
        else if (Speed < kFadeFar)
        {
            const double Fall = (Speed - kFadeNear) / (kFadeFar - kFadeNear);
            StoreAlchemyFpuFloat(Fade, (1.0f - Fall * Fall));
        }
        else
        {
            Fade = 0.0f;
        }
        const double JerkValue = std::fabs(static_cast<double>(LastCameraStep_) - StepLen);
        float Jerk;
        StoreAlchemyFpuFloat(Jerk, (JerkValue));
        if (JerkValue > kJerkLow)
        {
            if (!(Jerk < kJerkHigh))
                Fade = 0.0f;
            else
                StoreAlchemyFpuFloat(Fade, ((1.0 - (static_cast<double>(Jerk) - kJerkLow) /
                    (static_cast<double>(kJerkHigh) - kJerkLow)) * Fade));
        }

        if (AlDrawContextBegin(G_DustDrawContext, 4, VertCount, IndexCount))
        {
            float FrameBlock[5];
            std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
            const unsigned long Flags = Descriptor_->Flags;
            if ((Flags & kAppFlagFlipTexU) != 0)
            {
                const double Swap = LoadAppearanceScalar(FrameBlock[1]);
                std::memcpy(&FrameBlock[1], &FrameBlock[3], sizeof(float));
                StoreAlchemyFpuFloat(FrameBlock[3], (Swap));
            }
            if ((Flags & kAppFlagFlipTexV) != 0)
            {
                const double Swap = LoadAppearanceScalar(FrameBlock[2]);
                std::memcpy(&FrameBlock[2], &FrameBlock[4], sizeof(float));
                StoreAlchemyFpuFloat(FrameBlock[4], (Swap));
            }
            const float& U0 = FrameBlock[1];
            const float& V0 = FrameBlock[2];
            const float& U1 = FrameBlock[3];
            const float& V1 = FrameBlock[4];
            for (; Particle != nullptr; Particle = Particle->Next)
            {
                float* LastView =
                    reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(Particle) + 0x30);
                float C[3];
                Shared3DMath()->TransformXform(*reinterpret_cast<Vector*>(C),
                    ViewXform_,
                    *reinterpret_cast<const Vector*>(&Particle->PosX));

                if (!(C[2] > -1.0f) && !(LastView[2] > -1.0f))
                {
                    float InnerTime;
                    StoreAlchemyFpuFloat(InnerTime, (
                        static_cast<double>(Particle->InvDuration) * Particle->Age));
                    float Alpha;
                    StoreAlchemyFpuFloat(Alpha, (ForwardAppearanceSingleEval(
                            Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime) * Fade));
                    float HalfSize;
                    StoreAlchemyFpuFloat(HalfSize, (
                        ForwardAppearanceSingleEval(
                            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * 0.5));

                    float Mz;
                    StoreAlchemyFpuFloat(Mz, (static_cast<double>(C[2]) - LastView[2]));
                    float My;
                    StoreAlchemyFpuFloat(My, (static_cast<double>(C[1]) - LastView[1]));
                    float Mx;
                    StoreAlchemyFpuFloat(Mx, (static_cast<double>(C[0]) - LastView[0]));
                    const double Nx = static_cast<double>(C[1]) * Mz - static_cast<double>(C[2]) * My;
                    const double Ny = static_cast<double>(C[2]) * Mx - static_cast<double>(C[0]) * Mz;
                    const double Nz = static_cast<double>(C[0]) * My - static_cast<double>(C[1]) * Mx;
                    float Ux;
                    StoreAlchemyFpuFloat(Ux, (static_cast<double>(C[2]) * Ny - static_cast<double>(C[1]) * Nz));
                    float Uy;
                    StoreAlchemyFpuFloat(Uy, (static_cast<double>(C[0]) * Nz - static_cast<double>(C[2]) * Nx));
                    const double UnroundedUz = Nx * C[1] - static_cast<double>(C[0]) * Ny;
                    float LenSq;
                    StoreAlchemyFpuFloat(LenSq, ((static_cast<double>(Uy) * Uy +
                        static_cast<double>(Ux) * Ux) + UnroundedUz * UnroundedUz));
                    std::int32_t LengthBits;
                    std::memcpy(&LengthBits, &LenSq, sizeof(LengthBits));
                    const std::uint32_t GuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(LengthBits >> 1);
                    float Guess;
                    std::memcpy(&Guess, &GuessBits, sizeof(Guess));
                    const double GuessSquared = static_cast<double>(Guess) * Guess;
                    const double InvLen = (1.5 - ((GuessSquared * LenSq) * 0.5)) * Guess;
                    StoreAlchemyFpuFloat(Ux, (static_cast<double>(Ux) * InvLen));
                    StoreAlchemyFpuFloat(Uy, (static_cast<double>(Uy) * InvLen));
                    float Uz;
                    StoreAlchemyFpuFloat(Uz, (UnroundedUz * InvLen));

                    float ScaledMz;
                    StoreAlchemyFpuFloat(ScaledMz, (static_cast<double>(Mz) * kStreakScale));
                    float ScaledMy;
                    StoreAlchemyFpuFloat(ScaledMy, (static_cast<double>(My) * kStreakScale));
                    float ScaledMx;
                    StoreAlchemyFpuFloat(ScaledMx, (static_cast<double>(Mx) * kStreakScale));
                    const double InvDt = 1.0 / LastDt_;
                    float Wz;
                    StoreAlchemyFpuFloat(Wz, (ScaledMz * InvDt));
                    float Wy;
                    StoreAlchemyFpuFloat(Wy, (ScaledMy * InvDt));
                    float Wx;
                    StoreAlchemyFpuFloat(Wx, (ScaledMx * InvDt));
                    double Proj = ((static_cast<double>(Wy) * Uy + static_cast<double>(Wx) * Ux) +
                        static_cast<double>(Wz) * Uz) * 0.5;
                    if (Proj < 0.025)
                    {
                        Ux = 0.0f;
                        Uy = 1.0f;
                        Uz = 0.0f;
                    }
                    if (Proj > kStreakMax)
                        Proj = kStreakMax;
                    const double Reach = Proj + HalfSize;
                    float Az;
                    StoreAlchemyFpuFloat(Az, (Uz * Reach));
                    float Ay;
                    StoreAlchemyFpuFloat(Ay, (Uy * Reach));
                    float Ax;
                    StoreAlchemyFpuFloat(Ax, (Reach * Ux));
                    float Bz;
                    StoreAlchemyFpuFloat(Bz, (static_cast<double>(Az) * kTailScale));
                    float By;
                    StoreAlchemyFpuFloat(By, (static_cast<double>(Ay) * kTailScale));
                    float Bx;
                    StoreAlchemyFpuFloat(Bx, (static_cast<double>(Ax) * kTailScale));
                    float Dz;
                    StoreAlchemyFpuFloat(Dz, (static_cast<double>(Az) * kHeadScale));
                    float Dy;
                    StoreAlchemyFpuFloat(Dy, (static_cast<double>(Ay) * kHeadScale));
                    float Dx;
                    StoreAlchemyFpuFloat(Dx, (static_cast<double>(Ax) * kHeadScale));
                    float SideX;
                    StoreAlchemyFpuFloat(SideX, (-static_cast<double>(Uy) * HalfSize));
                    float SideY;
                    StoreAlchemyFpuFloat(SideY, (static_cast<double>(Ux) * HalfSize));

                    if (Alpha > kMinAlpha)
                    {
                        const unsigned long Color = ForwardAppearanceColorEval(
                            Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
                        const std::int64_t AlphaInteger = ConvertAlchemyFpuInteger(static_cast<double>(Alpha) * 255.0f);
                        const unsigned long Diffuse = Color | (static_cast<unsigned long>(AlphaInteger) << 24);
                        const double FirstZ = static_cast<double>(C[2]) - Dz;
                        const double FirstY = static_cast<double>(C[1]) - Dy;
                        unsigned short I0 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) - Dx);
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, FirstY);
                            StoreAlchemyFpuFloat(Vert->Z, FirstZ);
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U1));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V0));
                            I0 = G_DustDrawContext.VertCount;
                            ++G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                        }
                        unsigned short I1 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) + SideX);
                            const double SecondY = static_cast<double>(C[1]) + SideY;
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, SecondY);
                            StoreAlchemyFpuFloat(Vert->Z, LoadAppearanceScalar(C[2]));
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U1));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V1));
                            I1 = G_DustDrawContext.VertCount;
                            ++G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                        }
                        unsigned short I2 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) - SideX);
                            const double ThirdY = static_cast<double>(C[1]) - SideY;
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, ThirdY);
                            StoreAlchemyFpuFloat(Vert->Z, LoadAppearanceScalar(C[2]));
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U0));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V0));
                            I2 = G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                            ++G_DustDrawContext.VertCount;
                        }
                        const double LastY = static_cast<double>(C[1]) + By;
                        float LastZ;
                        StoreAlchemyFpuFloat(LastZ, static_cast<double>(C[2]) + Bz);
                        unsigned short I3 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) + Bx);
                            std::memcpy(&Vert->Z, &LastZ, sizeof(float));
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, LastY);
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U0));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V1));
                            I3 = G_DustDrawContext.VertCount;
                            ++G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                        }
                        StoreQuadIndices(G_DustDrawContext, I0, I1, I2, I3);
                    }
                }

                std::memcpy(LastView, C, sizeof(C));
            }

            if (AlDrawContextEnd(G_DustDrawContext) != 0)
            {
                ++G_DrawCounters[1];
                CallSetRenderState(G_RenderPipeline, D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
                CallSetRenderState(G_RenderPipeline, D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));
                unsigned long FrameTexture = 0;
                std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
                CallRenderPipelineSetTexture(G_RenderPipeline, 0, static_cast<int>(FrameTexture));
                Transform Identity;
                FillIdentity3x3(&Identity.Rotation);
                Identity.Translation.x = 0.0f;
                Identity.Translation.y = 0.0f;
                Identity.Translation.z = 0.0f;
                CallRenderPipelineSetTransform(G_RenderPipeline, &Identity);
                AlDrawContextDraw(G_DustDrawContext);
            }
        }

        std::memcpy(&LastCameraPos_, &CameraPos, sizeof(CameraPos));
        std::memcpy(&LastCameraStep_, &StepLen, sizeof(StepLen));
        return GR_OK;
    }
#pragma function(sqrt)
#pragma float_control(pop)
#pragma optimize("", on)

    FLBeamAppearance::FLBeamAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kBeamClass), bRibbonParity_(false)
    {
    }

    void FLBeamAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildBeamDescriptorRamps);
    }

    // FUN_06204ef0: as basic's clone.
    GENRESULT DACOM_API FLBeamAppearance::CloneNode(IDAComponent** Out)
    {
        FLBeamAppearance* const Node = new (std::nothrow) FLBeamAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    // LAB_0620e080 (outer +0x64) is the shared FGenericFxObjectBase::SetNodeAge.

    // FUN_0620e430 (outer +0xBC; node-vtable twin LAB_062055a0): a full pool
    // (DAT_06257964 == 0) evicts its oldest item (FUN_0620dde0); FUN_0620be90 then PREPENDS
    // the new item (unlike the billboards) with +0x30 = node+0xdc (zero from the node
    // ctors), and node+0xe1 toggles. The prepend is unconditional in retail even when the
    // allocation failed.
    GENRESULT DACOM_API FLBeamAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        if (G_PoolBeam.FreeCount == 0)
            EvictOldestListParticle(&G_PoolBeam);

        FPooledParticle* const Particle =
            static_cast<FPooledParticle*>(AllocPoolItem(&G_PoolBeam));
        if (Particle != nullptr)
        {
            Particle->PosX = Six[0];
            Particle->PosY = Six[1];
            Particle->PosZ = Six[2];
            Particle->VelX = Six[3];
            Particle->VelY = Six[4];
            Particle->VelZ = Six[5];
            Particle->Appearance = this;
            Particle->Age = Age;
            Particle->Duration = Duration;
            Particle->InvDuration = 1.0f / Duration;
            *reinterpret_cast<unsigned long*>(reinterpret_cast<unsigned char*>(Particle) + 0x30) = 0;
            ++LiveInstanceCount_;
        }
        PrependParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        bRibbonParity_ = !bRibbonParity_;
        return GR_OK;
    }

    // FUN_0620de30 (outer +0x14 of PTR_FUN_0624b7d0; node twin LAB_062054d0), the beam's
    // per-node update, reached from IAlchemy's update walk FUN_06201810 like every other
    // node's: the basic update without a frame step; fields FUN_0620bc30, integrate
    // FUN_0620bda0. Dt is not clamped here.
    #pragma float_control(except, on, push)
    GENRESULT DACOM_API FLBeamAppearance::UpdateNode(float Dt)
    {
        const double Delta = LoadAppearanceScalar(Dt);
        G_BeamDrawContext.Field_0xA = 0;
        G_BeamDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(Delta + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        FFxTextureKey Key;
        Key.Name = "???"; // DAT_06255804
        Key.Crc = Descriptor_->TextureCrc;
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolBeam, Dt);
        return GR_OK;
    }
    #pragma float_control(pop)

    namespace
    {
        // The peeled pass loads Z/Y/X before any FUN_06206ad0 products; the loop rounds
        // the inline YZ + X squared distance before FUN_06206b30.
        #pragma optimize("", off)
        #pragma float_control(except, on, push)
        __declspec(noinline) void StoreBeamBoundsSphere(float* Sphere,
            const FPooledParticle* Particle, const double& Frame, const float& Primary)
        {
            const double Product = Frame * Primary;
            std::uint32_t Word;
            std::memcpy(&Word, &Particle->PosX, sizeof(Word));
            std::memcpy(Sphere, &Word, sizeof(Word));
            std::memcpy(&Word, &Particle->PosY, sizeof(Word));
            std::memcpy(Sphere + 1, &Word, sizeof(Word));
            const double Radius = Product * 0.5;
            std::memcpy(&Word, &Particle->PosZ, sizeof(Word));
            std::memcpy(Sphere + 2, &Word, sizeof(Word));
            StoreAlchemyFpuFloat(Sphere[3], Radius);
        }

        __declspec(noinline) void MergeBeamBoundsSphere(float* Sphere, const float* Center,
            const float& Radius, EPerpRectBoundsDistance DistanceKind)
        {
            const double DifferenceZ = static_cast<double>(Center[2]) - Sphere[2];
            const double DifferenceY = static_cast<double>(Center[1]) - Sphere[1];
            const double DifferenceX = static_cast<double>(Center[0]) - Sphere[0];
            float Difference[3];
            StoreAlchemyFpuFloat(Difference[0], DifferenceX);
            StoreAlchemyFpuFloat(Difference[1], DifferenceY);
            StoreAlchemyFpuFloat(Difference[2], DifferenceZ);
            double LengthSquared;
            float RoundedLengthSquared;
            if (DistanceKind == EPerpRectBoundsDistance::PerpUnroundedXyz)
            {
                const double Z = LoadAppearanceScalar(Difference[2]);
                const double Y = LoadAppearanceScalar(Difference[1]);
                const double X = LoadAppearanceScalar(Difference[0]);
                const double SquareX = X * X;
                const double SquareY = Y * Y;
                const double SumXy = SquareX + SquareY;
                const double SquareZ = Z * Z;
                LengthSquared = SumXy + SquareZ;
                StoreAlchemyFpuFloat(RoundedLengthSquared, LengthSquared);
            }
            else
            {
                const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
                const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
                const double SumYz = SquareY + SquareZ;
                const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
                StoreAlchemyFpuFloat(RoundedLengthSquared, SumYz + SquareX);
                LengthSquared = RoundedLengthSquared;
            }
            std::int32_t LengthBits;
            std::memcpy(&LengthBits, &RoundedLengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            const double InverseLength = (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
            const double Distance = InverseLength * LengthSquared;
            float RoundedDistance;
            StoreAlchemyFpuFloat(RoundedDistance, Distance);
            // Each test below takes its branch only for an ordered result.
            if (Distance + Sphere[3] < Radius)
            {
                std::memcpy(Sphere, Center, sizeof(float) * 3);
                std::memcpy(Sphere + 3, &Radius, sizeof(Radius));
                return;
            }
            if (static_cast<double>(Radius) + RoundedDistance < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Radius) + Sphere[3]) + RoundedDistance) * 0.5);
            if (!(static_cast<double>(RoundedDistance) <= 1e-6))
            {
                const double InverseDistance = 1.0 / RoundedDistance;
                StoreAlchemyFpuFloat(Difference[0], InverseDistance * Difference[0]);
                StoreAlchemyFpuFloat(Difference[1], InverseDistance * Difference[1]);
                StoreAlchemyFpuFloat(Difference[2], InverseDistance * Difference[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Sphere[3]);
                const double StepZ = static_cast<double>(Step) * Difference[2];
                const double StepY = static_cast<double>(Step) * Difference[1];
                const double StepX = static_cast<double>(Step) * Difference[0];
                float Offset[3];
                StoreAlchemyFpuFloat(Offset[0], StepX);
                StoreAlchemyFpuFloat(Offset[1], StepY);
                StoreAlchemyFpuFloat(Offset[2], StepZ);
                StoreAlchemyFpuFloat(Sphere[0], static_cast<double>(Offset[0]) + Sphere[0]);
                StoreAlchemyFpuFloat(Sphere[1], static_cast<double>(Offset[1]) + Sphere[1]);
                StoreAlchemyFpuFloat(Sphere[2], static_cast<double>(Offset[2]) + Sphere[2]);
            }
            std::memcpy(Sphere + 3, &NewRadius, sizeof(NewRadius));
        }
        #pragma float_control(pop)
        #pragma optimize("", on)
    }

    // FUN_062050c0 (outer +0x8C through FUN_0620e1b0): the billboard bounds walk (see
    // FxBasicAppearance::GetBoundingSphere) over node+0xe4 with the beam radius, the peeled tail pass at the
    // first particle's t, and math engine DAT_06257978.
    #pragma float_control(except, on, push)
    GENRESULT DACOM_API FLBeamAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const double FirstAge = LoadAppearanceScalar(Head->Age);
        const float FirstTime = static_cast<float>(FirstAge * Head->InvDuration);
        const float Primary = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstTime));
        const double Frame = ForwardAppearanceSingleEval(
            Descriptor_->TexFrameRamp, AppearanceOuterTime_, FirstTime);
        StoreBeamBoundsSphere(OutSphere, Head, Frame, Primary);
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            // The peeled tail deliberately uses the head's current time again.
            const double TailAge = LoadAppearanceScalar(Head->Age);
            const float TailTime = static_cast<float>(TailAge * Head->InvDuration);
            const float TailPrimary = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailTime));
            const double TailFrame = ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, TailTime);
            float TailSphere[4];
            StoreBeamBoundsSphere(TailSphere, Tail, TailFrame, TailPrimary);
            MergeBeamBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float Time = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const float ParticlePrimary = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, Time));
                const double ParticleFrame = ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, Time);
                float Sphere[4];
                StoreBeamBoundsSphere(Sphere, Particle, ParticleFrame, ParticlePrimary);
                MergeBeamBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::RectRoundedYzx);
            }
        }
        Vector WorldCenter;
        TransformPerpBoundsCenter(NodeWorldXform_,
            WorldCenter, *reinterpret_cast<const Vector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }
    #pragma float_control(pop)

    // FUN_06205620, __stdcall on the node: the basic draw's shape, batch list 3, context
    // DAT_0625792c, fill FUN_06205720; the tail is the same sequence as LAB_062069e0.
    GENRESULT DACOM_API FLBeamAppearance::DrawOrEnqueue()
    {
        if (GetFirstInstanceParticle() == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(3, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_BeamDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        FillRibbon();

        if (AlDrawContextEnd(G_BeamDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];
        ApplyDrawState();
        AlDrawContextDraw(G_BeamDrawContext);
        return GR_OK;
    }

    // LAB_062069c0 (node vtable +0x4c, outer +0x9C): FUN_06240f50 on DAT_0625792c.
    GENRESULT DACOM_API FLBeamAppearance::BeginBatchedDraw(unsigned long Verts,
                                                           unsigned long Indices)
    {
        return AlDrawContextBegin(G_BeamDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // LAB_062069e0 (node vtable +0x50, outer +0xA0).
    GENRESULT DACOM_API FLBeamAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_BeamDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];
        ApplyDrawState();
        AlDrawContextDraw(G_BeamDrawContext);
        return GR_OK;
    }

    // LAB_06206a20 (node vtable +0x54, outer +0xA4).
    GENRESULT DACOM_API FLBeamAppearance::FillBatchedGeometry()
    {
        FillRibbon();
        return GR_OK;
    }

    // LAB_06206a30 (node vtable +0x7c, outer +0xD0): 5 per particle plus 5 for the cap
    // (flag 0x100). No null check on Out.
    GENRESULT DACOM_API FLBeamAppearance::GetQueuedVertCount(unsigned long* Out)
    {
        unsigned long Count = ResolveLiveInstanceCount() * 5;
        *Out = Count;
        if ((GetAppearanceRampDescriptor()->Flags & kAppFlagBeamDupeFirstParticle) == kAppFlagBeamDupeFirstParticle)
            *Out = Count + 5;
        return GR_OK;
    }

    // LAB_06206a70 (node vtable +0x80, outer +0xD4): 0x18 per particle plus 0x18 for the
    // cap.
    GENRESULT DACOM_API FLBeamAppearance::GetQueuedIndexCount(unsigned long* Out)
    {
        unsigned long Count = ResolveLiveInstanceCount() * 0x18;
        *Out = Count;
        if ((GetAppearanceRampDescriptor()->Flags & kAppFlagBeamDupeFirstParticle) == kAppFlagBeamDupeFirstParticle)
            *Out = Count + 0x18;
        return GR_OK;
    }

    // FUN_06206850, __thiscall on the node: flag 0x400 (line appearance) draws untextured
    // in wireframe (fill mode 2), else textured solid (3). Retail quirk kept: the fill-mode
    // saver is destroyed at the end of this function, so the fill mode is restored before
    // the caller's Draw. The beam's frame block sits 4 bytes later (node+0xc0) than on the
    // other kinds.
    void FLBeamAppearance::ApplyDrawState()
    {
        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));

        int FillMode = 0;
        unsigned long Texture = 0;
        if ((GetAppearanceRampDescriptor()->Flags & kAppFlagBeamLineAppearance) == kAppFlagBeamLineAppearance)
        {
            FillMode = 2;
            Texture = 0;
        }
        else
        {
            std::memcpy(&Texture, &Texture_.Frame[0], sizeof(Texture));
            FillMode = 3;
        }
        G_RenderPipeline->SetTextureStageTexture(0, reinterpret_cast<void*>(Texture));

        // Retail arms the unwind only after both GetRenderState and SetRenderState.
        struct FBeamFillModeState
        {
            IRenderPipeline* Pipeline;
            int Saved;
            FBeamFillModeState(IRenderPipeline* InPipeline, int InFillMode)
                : Pipeline(InPipeline)
            {
                Pipeline->GetRenderState(D3DRS_FILLMODE, &Saved);
                Pipeline->SetRenderState(D3DRS_FILLMODE, InFillMode);
            }
            ~FBeamFillModeState() noexcept(false)
            {
                Pipeline->SetRenderState(D3DRS_FILLMODE, Saved);
            }
        } SavedFillMode(G_RenderPipeline, FillMode);

        Transform ViewWorld;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        ComposeTransform(&ViewWorld, Camera->GetViewTransform(), NodeWorldXform_);
        G_RenderPipeline->SetModelview(&ViewWorld);
    }

    // FUN_06205720, __thiscall on the node: the beam ribbon.
    //   Tip: the first associated field whose factory name is "FLBeamField" pins the tip
    //     (its transform applied to minus node+0x4c's translation) when it is more than
    //     0.1 (ordered, squared 0.01) from the head; otherwise, with two or more particles
    //     and flag 0x200 clear, the tip is extrapolated back from the first segment by
    //     Head.Age / (Next.Age - Head.Age).
    //   Flag 0x100 draws a cap ring at the tip; without it the tip replaces the lead
    //     position.
    //   Rings follow every particle further than 0.1 from the last ring (length summed
    //     x, z, y). Retail quirk kept: Lead supplies the ramp time and Lag the position,
    //     one particle apart after an uncapped tip.
    // DAT_0625580c / DAT_06255808 (rail A / rail B switches) are .data bytes = 1 with no
    // writer, so both rails are always built. The UVs are Texture_.Frame[1..4], read
    // without the 0x1000 / 0x2000 flips. Retail leaves three ring indices and PrevPos
    // uninitialised; the port zeroes them.
    namespace
    {
        // Beam's vector helpers keep Z/Y/X calculations before X/Y/Z float stores.
        #pragma optimize("", off)
        #pragma float_control(except, on, push)
        __declspec(noinline) void BeamSubtract(float* Out, const float* A, const float* B)
        {
            const double Z = static_cast<double>(A[2]) - B[2];
            const double Y = static_cast<double>(A[1]) - B[1];
            const double X = static_cast<double>(A[0]) - B[0];
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) void BeamAdd(float* Out, const float* A, const float* B)
        {
            const double Z = static_cast<double>(A[2]) + B[2];
            const double Y = static_cast<double>(A[1]) + B[1];
            const double X = static_cast<double>(A[0]) + B[0];
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) void BeamScale(float* Out, const float* Value, const float& Scale)
        {
            const double Z = static_cast<double>(Scale) * Value[2];
            const double Y = static_cast<double>(Scale) * Value[1];
            const double X = static_cast<double>(Scale) * Value[0];
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) void BeamBasis(float* Out, const float* Matrix, int Column)
        {
            const double Z = LoadAppearanceScalar(Matrix[Column + 6]);
            const double Y = LoadAppearanceScalar(Matrix[Column + 3]);
            const double X = LoadAppearanceScalar(Matrix[Column]);
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) bool BeamLongSegmentXyz(const float* Value)
        {
            const double X = static_cast<double>(Value[0]) * Value[0];
            const double Y = static_cast<double>(Value[1]) * Value[1];
            const double Xy = X + Y;
            const double Z = static_cast<double>(Value[2]) * Value[2];
            return Xy + Z > 0.01f;
        }
        __declspec(noinline) void BeamNegate(float* Out, const float* Value)
        {
            const double Z = -static_cast<double>(Value[2]);
            const double Y = -static_cast<double>(Value[1]);
            const double X = -static_cast<double>(Value[0]);
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) double BeamExtrapolation(const float& NextAge, const float& HeadAge)
        {
            const double Denominator = static_cast<double>(NextAge) - HeadAge;
            return static_cast<double>(HeadAge) / Denominator;
        }
        __declspec(noinline) bool BeamLongSegmentXzy(const float* Value)
        {
            const double X = static_cast<double>(Value[0]) * Value[0];
            const double Z = static_cast<double>(Value[2]) * Value[2];
            const double Xz = X + Z;
            const double Y = static_cast<double>(Value[1]) * Value[1];
            return Xz + Y > 0.01f;
        }
        // FUN_06208aa0's cap arguments are raw words. FUN_06206020's inline writer converts
        // them through x87 only after its capacity gate (V, U, Z, Y, X, in that order).
        __declspec(noinline) unsigned short StoreBeamRawVertex(const float* Pos,
            unsigned long Diffuse, const float& U, const float& V)
        {
            const long Count = G_BeamDrawContext.VertCount;
            if (Count > static_cast<long>(G_BeamDrawContext.RequestedVerts))
                return static_cast<unsigned short>(G_BeamDrawContext.RequestedVerts);
            FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BeamDrawContext.LockedVerts);
            std::memcpy(&Vertex->X, Pos, 3 * sizeof(float));
            Vertex->Diffuse = Diffuse;
            std::memcpy(&Vertex->U, &U, sizeof(float));
            std::memcpy(&Vertex->V, &V, sizeof(float));
            G_BeamDrawContext.LockedVerts = Vertex + 1;
            G_BeamDrawContext.VertCount = static_cast<unsigned short>(Count + 1);
            return static_cast<unsigned short>(Count);
        }
        __declspec(noinline) unsigned short StoreBeamConvertedVertex(const float* Pos,
            unsigned long Diffuse, const float& U, const float& V)
        {
            const long Count = G_BeamDrawContext.VertCount;
            if (Count > static_cast<long>(G_BeamDrawContext.RequestedVerts))
                return static_cast<unsigned short>(G_BeamDrawContext.RequestedVerts);
            FAlchemyVertex Words;
            Words.V = static_cast<float>(LoadAppearanceScalar(V));
            Words.U = static_cast<float>(LoadAppearanceScalar(U));
            const double Z = LoadAppearanceScalar(Pos[2]);
            FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BeamDrawContext.LockedVerts);
            Words.Z = static_cast<float>(Z);
            Words.Y = static_cast<float>(LoadAppearanceScalar(Pos[1]));
            Words.X = static_cast<float>(LoadAppearanceScalar(Pos[0]));
            Words.Diffuse = Diffuse;
            std::memcpy(Vertex, &Words, sizeof(Words));
            G_BeamDrawContext.LockedVerts = Vertex + 1;
            G_BeamDrawContext.VertCount = static_cast<unsigned short>(Count + 1);
            return static_cast<unsigned short>(Count);
        }
        #pragma float_control(pop)
        #pragma optimize("", on)
    }

    #pragma float_control(except, on, push)
    void FLBeamAppearance::FillRibbon()
    {
        static const char kBeamFieldName[12] = "FLBeamField";
        FPooledParticle* Head = InstanceListHead_;
        ++G_DrawCounters[7];

        float Tip[3] = {0.0f, 0.0f, 0.0f};
        bool bHaveTip = false;
        bool bFieldTip = false;
        const long FieldTotal = static_cast<long>(FieldCount_);
        for (long FieldIndex = 0; FieldIndex < FieldTotal; ++FieldIndex)
        {
            IGenericFx* Field = AsFxNode(AssociatedFields_[FieldIndex]);
            const char* Name = Field->GetFxFactoryName();
            bool bMatch = true;
            for (int Byte = 0; Byte < 12; ++Byte)
            {
                if (Name[Byte] != kBeamFieldName[Byte])
                {
                    bMatch = false;
                    break;
                }
            }
            if (!bMatch)
            {
                continue;
            }

            Transform* FieldWorld = nullptr;
            Field->GetNodeTransform(0, &FieldWorld);
            float NegNodePos[3];
            BeamNegate(NegNodePos, &NodeWorldXform_.Translation.x);
            TransformPerpBoundsCenter(*FieldWorld,
                *reinterpret_cast<Vector*>(Tip),
                *reinterpret_cast<const Vector*>(NegNodePos));
            float Difference[3];
            BeamSubtract(Difference, &Head->PosX, Tip);
            if (BeamLongSegmentXyz(Difference))
            {
                bHaveTip = true;
                bFieldTip = true;
            }
            break;
        }

        if (!bFieldTip)
        {
            if (static_cast<long>(LiveInstanceCount_) < 2)
                return;
            const FPooledParticle* Next = Head->Next;
            if ((Descriptor_->Flags & kAppFlagBeamDisablePlaceHolder) != kAppFlagBeamDisablePlaceHolder)
            {
                float Difference[3];
                BeamSubtract(Difference, &Next->PosX, &Head->PosX);
                if (BeamLongSegmentXyz(Difference))
                {
                    const float K = static_cast<float>(BeamExtrapolation(Next->Age, Head->Age));
                    BeamScale(Difference, Difference, K);
                    BeamSubtract(Tip, &Head->PosX, Difference);
                    bHaveTip = true;
                }
            }
        }

        FBeamRingState State;
        State.Idx[0] = 0xffff;
        State.Idx[1] = 0;
        State.Idx[2] = 0;
        State.Idx[3] = 0;
        State.Counter = 0;
        State.Node = this;
        FillIdentity3x3(&State.Xform.Rotation);
        State.Xform.Translation.x = 0.0f;
        State.Xform.Translation.y = 0.0f;
        State.Xform.Translation.z = 0.0f;
        State.PrevPos[0] = 0.0f;
        State.PrevPos[1] = 0.0f;
        State.PrevPos[2] = 0.0f;
        Transform Basis;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        ComposeTransform(&Basis, (const Transform*)Camera->GetOrientation(), NodeWorldXform_);
        std::memcpy(&State.Xform, &Basis, sizeof(Basis));

        const float* LeadPos = &Head->PosX;
        bool bTipNoCap = false;
        if (bHaveTip)
        {
            if ((Descriptor_->Flags & kAppFlagBeamDupeFirstParticle) == kAppFlagBeamDupeFirstParticle)
            {
                float HalfWidth = 0.0f;
                float Rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float V = 0.0f;
                EvalRingParams(State, Head, &HalfWidth, Rgba, &V);
                const unsigned long Diffuse = PackRingColor(Rgba);
                unsigned short NewIdx[4];
                float BasisColumn[3];
                float Side[3];
                float VertexPos[3];
                BeamBasis(BasisColumn, &State.Xform.Rotation.E[0][0], 0);
                const float WidthA = static_cast<float>(LoadAppearanceScalar(HalfWidth));
                BeamScale(Side, BasisColumn, WidthA);
                BeamSubtract(VertexPos, Tip, Side);
                NewIdx[0] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
                BeamAdd(VertexPos, Tip, Side);
                NewIdx[1] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);
                BeamBasis(BasisColumn, &State.Xform.Rotation.E[0][0], 1);
                const float WidthB = static_cast<float>(LoadAppearanceScalar(HalfWidth));
                BeamScale(Side, BasisColumn, WidthB);
                BeamSubtract(VertexPos, Tip, Side);
                NewIdx[2] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
                BeamAdd(VertexPos, Tip, Side);
                NewIdx[3] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);

                if (State.Idx[0] != 0xffff)
                    JoinRing(State, Tip, Diffuse, NewIdx);

                std::memcpy(State.PrevPos, Tip, 3 * sizeof(float));
                State.Idx[0] = NewIdx[0];
                State.Idx[1] = NewIdx[1];
                State.Idx[2] = NewIdx[2];
                State.Idx[3] = NewIdx[3];
                ++State.Counter;
            }
            else
            {
                bTipNoCap = true;
                LeadPos = Tip;
            }
        }

        EmitRing(State, Head, LeadPos);

        const FPooledParticle* Lead = Head->Next;
        const FPooledParticle* Lag = bTipNoCap ? InstanceListHead_ : Lead;
        const float* Last = LeadPos;
        while (Lead != nullptr)
        {
            float Difference[3];
            BeamSubtract(Difference, &Lag->PosX, Last);
            if (BeamLongSegmentXzy(Difference))
            {
                EmitRing(State, Lead, &Lag->PosX);
                Last = &Lag->PosX;
            }
            Lead = Lead->Next;
            Lag = Lag->Next;
        }
    }
    #pragma float_control(pop)

    // FUN_06205f00, __thiscall on the ring state: half width, colour and V for one ring
    // (V alternates by ring parity against node+0xe1). FUN_06206020 opens with the same
    // sequence inline.
    #pragma float_control(except, on, push)
    void FLBeamAppearance::EvalRingParams(const FBeamRingState& State, const FPooledParticle* P,
                                          float* OutHalfWidth, float* OutRgba, float* OutV)
    {
        const float T = P->InvDuration * P->Age;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->EffectScaleRamp, State.Node->AppearanceOuterTime_,
            State.Node->LodInput_));
        const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->PrimarySizeRamp, State.Node->AppearanceOuterTime_, T) * Scale);
        *OutHalfWidth = static_cast<float>((ForwardAppearanceSingleEval(
            State.Node->Descriptor_->TexFrameRamp, State.Node->AppearanceOuterTime_, T) * SizeScale) * 0.5);
        const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->SizeRamp, State.Node->AppearanceOuterTime_, T));
        float Rgb[3];
        ForwardAppearanceRgbEval(State.Node->Descriptor_->ColorRamp, Rgb,
            State.Node->AppearanceOuterTime_, T);
        // Only red is loaded through x87; green, blue and alpha are raw words.
        const double Red = LoadAppearanceScalar(Rgb[0]);
        std::memcpy(&OutRgba[1], &Rgb[1], sizeof(float));
        std::memcpy(&OutRgba[2], &Rgb[2], sizeof(float));
        OutRgba[0] = static_cast<float>(Red);
        std::memcpy(&OutRgba[3], &Alpha, sizeof(float));
        const FLBeamAppearance* const Node = State.Node;
        const long Parity = Node->bRibbonParity_ ? 1 : 0;
        const float* const V = ((State.Counter & 1) == Parity)
            ? &Node->Texture_.Frame[2] : &Node->Texture_.Frame[4];
        std::memcpy(OutV, V, sizeof(float));
    }
    #pragma float_control(pop)

    // The callers' packing of FUN_06205f00's {R, G, B, A} (also inline in FUN_06206020).
    // Alpha is not masked; its high bits shift out.
    #pragma optimize("", off)
    #pragma float_control(except, on, push)
    __declspec(noinline) unsigned long FLBeamAppearance::PackRingColor(const float* Rgba)
    {
        unsigned long Packed = static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[3]) * 255.0f));
        Packed = (Packed << 8) |
                 (static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[0]) * 255.0f)) & 0xffu);
        Packed = (Packed << 8) |
                 (static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[1]) * 255.0f)) & 0xffu);
        Packed = (Packed << 8) |
                 (static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[2]) * 255.0f)) & 0xffu);
        return Packed;
    }
    #pragma float_control(pop)
    #pragma optimize("", on)

    // FUN_06206020, __thiscall on the ring state: one ring of two rails (rail A along the
    // view's first column, rail B along its second), joined to the previous ring by two
    // quads, or with flag 2 by eight triangles around a centre vertex.
    #pragma float_control(except, on, push)
    void FLBeamAppearance::EmitRing(FBeamRingState& State, const FPooledParticle* P,
                                    const float* Pos)
    {
        const float T = P->InvDuration * P->Age;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->EffectScaleRamp, State.Node->AppearanceOuterTime_,
            State.Node->LodInput_));
        const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->PrimarySizeRamp, State.Node->AppearanceOuterTime_, T) * Scale);
        const float HalfWidth = static_cast<float>((ForwardAppearanceSingleEval(
            State.Node->Descriptor_->TexFrameRamp, State.Node->AppearanceOuterTime_, T) * SizeScale) * 0.5f);
        const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->SizeRamp, State.Node->AppearanceOuterTime_, T));
        float Rgba[4];
        ForwardAppearanceRgbEval(State.Node->Descriptor_->ColorRamp, Rgba,
            State.Node->AppearanceOuterTime_, T);
        std::memcpy(&Rgba[3], &Alpha, sizeof(float));
        float V;
        const FLBeamAppearance* const Node = State.Node;
        const float* const VWord = ((State.Counter & 1) == (Node->bRibbonParity_ ? 1 : 0))
            ? &Node->Texture_.Frame[2] : &Node->Texture_.Frame[4];
        std::memcpy(&V, VWord, sizeof(float));
        const unsigned long Diffuse = PackRingColor(Rgba);

        unsigned short NewIdx[4];
        float BasisColumn[3];
        float Side[3];
        float VertexPos[3];
        // The inline rail-A extraction loads/stores X, Y, Z successively.
        BasisColumn[0] = static_cast<float>(LoadAppearanceScalar(State.Xform.Rotation.E[0][0]));
        BasisColumn[1] = static_cast<float>(LoadAppearanceScalar(State.Xform.Rotation.E[1][0]));
        BasisColumn[2] = static_cast<float>(LoadAppearanceScalar(State.Xform.Rotation.E[2][0]));
        const float WidthA = static_cast<float>(LoadAppearanceScalar(HalfWidth));
        BeamScale(Side, BasisColumn, WidthA);
        BeamSubtract(VertexPos, Pos, Side);
        NewIdx[0] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
        BeamAdd(VertexPos, Pos, Side);
        NewIdx[1] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);
        // Unlike rail A, rail B multiplies the loaded X/Y/Z values before storing.
        const double Bx = LoadAppearanceScalar(State.Xform.Rotation.E[0][1]);
        const double By = LoadAppearanceScalar(State.Xform.Rotation.E[1][1]);
        const double Bz = LoadAppearanceScalar(State.Xform.Rotation.E[2][1]);
        StoreAlchemyFpuFloat(Side[2], Bz * HalfWidth);
        StoreAlchemyFpuFloat(Side[1], By * HalfWidth);
        StoreAlchemyFpuFloat(Side[0], Bx * HalfWidth);
        BeamSubtract(VertexPos, Pos, Side);
        NewIdx[2] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
        BeamAdd(VertexPos, Pos, Side);
        NewIdx[3] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);

        if (State.Idx[0] != 0xffff)
            JoinRing(State, Pos, Diffuse, NewIdx);

        State.Idx[0] = NewIdx[0];
        State.Idx[1] = NewIdx[1];
        State.Idx[2] = NewIdx[2];
        State.Idx[3] = NewIdx[3];
        std::memcpy(State.PrevPos, Pos, 3 * sizeof(float));
        ++State.Counter;
    }
    #pragma float_control(pop)

    // Inline in FUN_06205720 and FUN_06206020, identical in both: flag 2 -> centre vertex
    // ((Pos + PrevPos) * 0.5, colour, U = V = 0.5) and eight triangles; else two quads.
    #pragma float_control(except, on, push)
    void FLBeamAppearance::JoinRing(const FBeamRingState& State, const float* Pos,
                                    unsigned long Diffuse, const unsigned short* NewIdx)
    {
        if ((State.Node->Descriptor_->Flags & kAppFlagQuadTexture) == kAppFlagQuadTexture)
        {
            float Middle[3];
            BeamAdd(Middle, Pos, State.PrevPos);
            const float Half = 0.5f;
            const double Z = static_cast<double>(Middle[2]) * Half;
            StoreAlchemyFpuFloat(Middle[2], Z);
            const double Y = static_cast<double>(Middle[1]) * Half;
            StoreAlchemyFpuFloat(Middle[1], Y);
            const double X = static_cast<double>(Middle[0]) * Half;
            StoreAlchemyFpuFloat(Middle[0], X);
            const unsigned short Mid = StoreBeamConvertedVertex(Middle, Diffuse, Half, Half);
            StoreBeamTriangle(State.Idx[0], State.Idx[1], Mid);
            StoreBeamTriangle(State.Idx[1], NewIdx[1], Mid);
            StoreBeamTriangle(NewIdx[1], NewIdx[0], Mid);
            StoreBeamTriangle(NewIdx[0], State.Idx[0], Mid);
            StoreBeamTriangle(State.Idx[2], State.Idx[3], Mid);
            StoreBeamTriangle(State.Idx[3], NewIdx[3], Mid);
            StoreBeamTriangle(NewIdx[3], NewIdx[2], Mid);
            StoreBeamTriangle(NewIdx[2], State.Idx[2], Mid);
        }
        else
        {
            StoreBeamQuad(State.Idx[0], State.Idx[1], NewIdx[0], NewIdx[1]);
            StoreBeamQuad(State.Idx[2], State.Idx[3], NewIdx[2], NewIdx[3]);
        }
    }
    #pragma float_control(pop)

    FxMeshAppearance::FxMeshAppearance(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), MeshPropertySet_(nullptr)
    {
    }

    FxMeshAppearance::~FxMeshAppearance()
    {
    }

    // FUN_06224b60: operator_new(0x90), outer vtable 0x0624df90 (cached at node+0xc0 in
    // retail; the port reads it through MeshPropertySet_).
    void FxMeshAppearance::CreatePropertySet()
    {
        FFxMeshAppearancePropertySet* const Set = new FFxMeshAppearancePropertySet();
        AdoptPropertySet(Set);
        MeshPropertySet_ = Set;
    }

    // FUN_06224a90: as basic's clone, with the set cache at +0xc0.
    GENRESULT DACOM_API FxMeshAppearance::CloneNode(IDAComponent** Out)
    {
        FxMeshAppearance* const Node = new FxMeshAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->MeshPropertySet_ = MeshPropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_06225000: list order, point spheres, then the node transform even if empty.
    GENRESULT DACOM_API FxMeshAppearance::GetBoundingSphere(float* OutSphere)
    {
        unsigned long Count = 0;
        FPooledParticle* Particle = InstanceListHead_;
        while (Particle != nullptr)
        {
            if (Count == 0)
            {
                // The radius is written before the Z word is read.
                std::memcpy(&OutSphere[0], &Particle->PosX, sizeof(float));
                std::memcpy(&OutSphere[1], &Particle->PosY, sizeof(float));
                OutSphere[3] = 0.0f;
                std::memcpy(&OutSphere[2], &Particle->PosZ, sizeof(float));
            }
            else
            {
                float Center[3];
                std::memcpy(Center, &Particle->PosX, sizeof(Center));
                MergeMeshBoundsPoint(OutSphere, Center);
            }
            Particle = Particle->Next;
            ++Count;
        }
        if (G_3DMath == nullptr)
            G_3DMath = Create3DMath();
        Vector WorldCenter;
        G_3DMath->TransformXform(WorldCenter, NodeWorldXform_,
                                *reinterpret_cast<const Vector*>(OutSphere));
        std::memcpy(OutSphere, &WorldCenter, sizeof(WorldCenter));
        return Count == 0 ? GR_GENERIC : GR_OK;
    }

    // ---- FFxMeshAppearancePropertySet's slots (inner 0x0624dff0; +0x3c is FUN_06244330
    // itself, inherited from FFxAppearancePropertySet) ----

    // FUN_06226b80: the byte as stored.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropBool(unsigned long Hash,
                                                                  unsigned char* Out)
    {
        if (Hash != kMeshUseParticleTransformHash)
            return GR_ALE_CREATE_FAIL;
        *Out = bUseParticleTransform;
        return GR_OK;
    }

    // FUN_06226bb0: stored as passed (not normalised to 0/1).
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::SetPropBool(unsigned long Hash,
                                                                  unsigned char Value)
    {
        if (Hash != kMeshUseParticleTransformHash)
            return GR_ALE_CREATE_FAIL;
        bUseParticleTransform = Value;
        return GR_OK;
    }

    // FUN_06226be0 / FUN_06226c10: MeshId.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropInt(unsigned long Hash,
                                                                 unsigned long* Out)
    {
        if (Hash != kMeshIdHash)
            return GR_ALE_CREATE_FAIL;
        *Out = MeshId;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxMeshAppearancePropertySet::SetPropInt(unsigned long Hash,
                                                                 unsigned long Value)
    {
        if (Hash != kMeshIdHash)
            return GR_ALE_CREATE_FAIL;
        MeshId = Value;
        return GR_OK;
    }

    // FUN_06226cf0; else FUN_0623f9a0.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropName(unsigned long Hash,
                                                                  const char** Out)
    {
        if (Hash != kMeshNameHash)
            return FFxNodePropertySet::GetPropName(Hash, Out);
        *Out = MeshName.Buffer;
        return GR_OK;
    }

    // FUN_06226d20; else FUN_0623f9e0.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::SetPropName(unsigned long Hash,
                                                                  const char* Text)
    {
        if (Hash != kMeshNameHash)
            return FFxNodePropertySet::SetPropName(Hash, Text);
        MeshName.Assign(Text);
        return GR_OK;
    }

    // FUN_06226b40; else FUN_0623fb50.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropTransform(unsigned long Hash,
                                                                       IAnimatedTransform** Out)
    {
        if (Hash != kMeshParticleTransformHash)
            return FFxNodePropertySet::GetPropTransform(Hash, Out);
        *Out = ParticleTransform;
        (*Out)->AddRef();
        return GR_OK;
    }

    // FUN_06226e10: FUN_06244320, then the table 0x06256298 unless it returned non-zero.
    int DACOM_API FFxMeshAppearancePropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                               void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kMeshPropertyRows, Callback, Context);
    }

    // FUN_06224c20: (this, Six, Age, Duration),
    // always returns 0. Reached through vtable 0x0624dd90 +0x68 and the this+0xc adjustor
    // thunk FUN_06227330.
    GENRESULT DACOM_API FxMeshAppearance::EmitOne(const float* Six, unsigned long Extra,
                                                  float Duration)
    {
        float Age = 0.0f;
        std::memcpy(&Age, &Extra, 4);

        // A full mesh pool (DAT_06257a58 == 0) first gives up its oldest particle through
        // its owner (FUN_06224fd0 -> FUN_062257f0; the mesh cleanup is FreePoolItem's mesh
        // branch).
        if (G_PoolMesh.FreeCount == 0)
        {
            FPooledParticle* const Oldest = ParticleFromUsedLink(&G_PoolMesh, G_PoolMesh.UsedHead);
            FxMeshAppearance* const Owner = static_cast<FxMeshAppearance*>(
                static_cast<FGenericFxObjectBase*>(Oldest->Appearance));
            UnlinkParticleNode(&Owner->InstanceListHead_, &Owner->InstanceListTail_, Oldest);
            if (Owner->LiveInstanceCount_ != 0)
                --Owner->LiveInstanceCount_;
            ReleaseAppearancePoolItem(&G_PoolMesh, Oldest);
        }

        // The factory's out-param, released on every exit.
        IFxMesh* Mesh = nullptr;

        // No bound factory (DAT_06257b9c): nothing emitted.
        if (G_FxMeshFactory == nullptr)
            return GR_OK;

        Transform* World12 = nullptr;
        GetNodeTransform(0, &World12);

        // Both halves of Six are transformed as points (math engine DAT_06257a6c) -- the
        // velocity picks up the translation too, as in retail.
        I3DMathEngine* const Math = Acquire3DMath();
        float WorldSix[6];
        Math->TransformXform(*reinterpret_cast<Vector*>(&WorldSix[0]),
                             *World12,
                             *reinterpret_cast<const Vector*>(&Six[0])); // +0x28
        Math->TransformXform(*reinterpret_cast<Vector*>(&WorldSix[3]),
                             *World12,
                             *reinterpret_cast<const Vector*>(&Six[3])); // +0x28

        // IFxMeshFactory slot 3 with the set's MeshId (+0x60) and MeshName buffer (+0x74,
        // never null: assigned "" at construction). The only callee, freelancer.exe
        // FUN_004f6810, reads neither argument.
        FFxMeshAppearancePropertySet* const Set = MeshProps();
        void* const MeshIdArg = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Set->MeshId));
        void* const MeshNameArg = Set->MeshName.Buffer;
        IFxMeshFactory* const Factory = static_cast<IFxMeshFactory*>(G_FxMeshFactory);
        if (Factory->CreateMesh(MeshIdArg, MeshNameArg, WorldSix, &Mesh) != 0)
        {
            if (Mesh != nullptr)
                Mesh->Release();
            return GR_OK;
        }

        // MeshApp_UseParticleTransform: World * (the caller's LOCAL position) * the
        // animated transform at Age / Duration, into IFxMesh slot 3. The transform is always
        // present (built by FUN_06224710, filled in place by the .ale reader), so retail
        // tests only the byte.
        if (Set->bUseParticleTransform != 0)
        {
            Transform Anim = { Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                               { 0.0f, 0.0f, 0.0f } };
            {
                const float Phase = Age / Duration;
                unsigned long TimeA = 0;
                unsigned long TimeB = 0;
                std::memcpy(&TimeA, &AppearanceOuterTime_, sizeof(TimeA));
                std::memcpy(&TimeB, &Phase, sizeof(TimeB));
                Set->ParticleTransform->Evaluate(TimeA, TimeB, &Anim);
            }
            const Transform T = { Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                                  { Six[0], Six[1], Six[2] } };
            Transform A;
            Math->MulTransform(A,
                               *World12,
                               T); // +0x1C
            Transform B;
            Math->MulTransform(B,
                               A,
                               Anim); // +0x1C
            Mesh->SetTransform(&B);
        }

        // A counted local copy, released by FUN_06225940.
        IFxMesh* const LocalMesh = Mesh;
        if (LocalMesh != nullptr)
            LocalMesh->AddRef();

        FPooledParticle* const Particle = static_cast<FPooledParticle*>(AllocPoolItem(&G_PoolMesh));
        if (Particle != nullptr)
        {
            // The caller's Six (not the transformed copy); the mesh through FUN_062257b0
            // (AddRef, no Release of the old).
            Particle->PosX = Six[0];
            Particle->PosY = Six[1];
            Particle->PosZ = Six[2];
            Particle->VelX = Six[3];
            Particle->VelY = Six[4];
            Particle->VelZ = Six[5];
            Particle->Appearance = static_cast<FGenericFxObjectBase*>(this);
            Particle->Duration = Duration;
            Particle->Age = Age;
            AssignComPtr(reinterpret_cast<IDAComponent**>(
                             reinterpret_cast<unsigned char*>(Particle) + 0x30),
                         LocalMesh);
            Particle->InvDuration = 1.0f / Particle->Duration;
        }

        // FUN_06226690 appends the item to this+0xb4 -- a null item included, which faults
        // on its first store as retail does. LiveInstanceCount_ is the port's count.
        AppendParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        ++LiveInstanceCount_;

        if (LocalMesh != nullptr)
            LocalMesh->Release();

        if (Mesh != nullptr)
            Mesh->Release();
        return GR_OK;
    }

    // FUN_06225470 (mesh node vtable 0x0624dd90 +0xC; outer +0x14). Dead fields are erased
    // from the array (FUN_06226600). Live particles (Age <= Life or unordered) move and
    // tick their IFxMesh (slot 5, Dt); dead ones go through FUN_062257f0. Retail keeps no
    // count on this list; LiveInstanceCount_ is the port's. Last, the bounds (FUN_06225000)
    // are computed into an unused local, kept for their math-engine side effects.
    GENRESULT DACOM_API FxMeshAppearance::UpdateNode(float Dt)
    {
        Age_ += Dt;
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        IDAComponent** Field = AssociatedFields_;
        IDAComponent** FieldEnd = AssociatedFields_ + FieldCount_;
        while (Field != FieldEnd)
        {
            if (static_cast<unsigned char>(AsFxNode(*Field)->IsNodeDead()) != 0)
            {
                Field = EraseAssociatedField(Field);
                FieldEnd = AssociatedFields_ + FieldCount_;
                continue;
            }
            AsFxNode(*Field)->BeginField(static_cast<IGenericFx*>(this), 0.0f);
            FPooledParticle* FieldParticle = InstanceListHead_;
            while (FieldParticle != nullptr)
            {
                AsFxNode(*Field)->ApplyFieldToVel(&FieldParticle->PosX, &FieldParticle->Age, 0.0f);
                FieldParticle = FieldParticle->Next;
            }
            AsFxNode(*Field)->EndField();
            ++Field;
        }

        unsigned long DtBits = 0;
        std::memcpy(&DtBits, &Dt, sizeof(DtBits));
        FPooledParticle* Particle = InstanceListHead_;
        while (Particle != nullptr)
        {
            Particle->Age += Dt;
            if (!IsMeshParticleExpired(*Particle))
            {
                float Step[3];
                MeshParticleStep(*Particle, Dt, Step);
                Particle->PosX += Step[0];
                Particle->PosY += Step[1];
                Particle->PosZ += Step[2];
                IFxMesh* const ParticleMesh = *reinterpret_cast<IFxMesh**>(
                    reinterpret_cast<unsigned char*>(Particle) + 0x30);
                ParticleMesh->Update(reinterpret_cast<void*>(static_cast<std::uintptr_t>(DtBits)));
                Particle = Particle->Next;
            }
            else
            {
                FPooledParticle* const Next = Particle->Next;
                UnlinkParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
                --LiveInstanceCount_;
                ReleaseAppearancePoolItem(&G_PoolMesh, Particle);
                Particle = Next;
            }
        }
        float Bounds[4];
        GetBoundingSphere(Bounds);
        return GR_OK;
    }

    // FUN_06225240. Reached from the outer
    // +0x10 thunk FUN_06226e60 (this+0xc, both stack arguments passed through).
    //   The shared compose FUN_062401a0 first; then per instance with a mesh: with
    //   MeshApp_UseParticleTransform the spawn's World * T * Anim, else the mesh's own
    //   transform (slot 4) with the translation replaced by the world position; both into
    //   mesh slot 3.
    GENRESULT DACOM_API FxMeshAppearance::ComposeNodeTransform(void* Unused, const Transform* Parent)
    {
        const GENRESULT Result = FGenericFxObjectBase::ComposeNodeTransform(Unused, Parent);
        if (Result != GR_OK)
            return Result;

        Transform* World = nullptr;
        GetNodeTransform(0, &World);

        FPooledParticle* Item = InstanceListHead_;
        while (Item != nullptr)
        {
            IFxMesh* const Mesh = *reinterpret_cast<IFxMesh**>(
                reinterpret_cast<unsigned char*>(Item) + 0x30);
            if (Mesh != nullptr)
            {
                I3DMathEngine* const Math = Acquire3DMath();
                FFxMeshAppearancePropertySet* const Set = MeshProps();
                if (Set->bUseParticleTransform != 0)
                {
                    Transform Anim = { Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                                       { 0.0f, 0.0f, 0.0f } };
                    {
                        const float Phase = Item->Age / Item->Duration;
                        unsigned long TimeA = 0;
                        unsigned long TimeB = 0;
                        std::memcpy(&TimeA, &AppearanceOuterTime_, sizeof(TimeA));
                        std::memcpy(&TimeB, &Phase, sizeof(TimeB));
                        Set->ParticleTransform->Evaluate(TimeA, TimeB, &Anim);
                    }
                    const Transform T = { Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                                          { Item->PosX, Item->PosY, Item->PosZ } };
                    Transform X;
                    Math->MulTransform(X,
                                       *World,
                                       T); // +0x1C
                    Transform Y;
                    Math->MulTransform(Y,
                                       X,
                                       Anim); // +0x1C
                    Mesh->SetTransform(&Y);
                }
                else
                {
                    Transform M;
                    std::memcpy(&M, Mesh->GetTransform(), sizeof(M));
                    Vector WorldPos;
                    Math->TransformXform(WorldPos,
                                         *World,
                                         *reinterpret_cast<const Vector*>(&Item->PosX)); // +0x28
                    M.Translation.x = WorldPos.x;
                    M.Translation.y = WorldPos.y;
                    M.Translation.z = WorldPos.z;
                    Mesh->SetTransform(&M);
                }
            }
            Item = Item->Next;
        }
        return Result;
    }

    // FUN_062256a0, reached through the outer +0x18
    // thunk FUN_06226ea0: IFxMesh slot 6 per instance with DAT_06257908, the draw walk's
    // camera cookie; no null check on the mesh (an item is linked only after the factory
    // handed one out).
    GENRESULT DACOM_API FxMeshAppearance::DrawOrEnqueue()
    {
        FPooledParticle* Item = InstanceListHead_;
        while (Item != nullptr)
        {
            IFxMesh* const Mesh = *reinterpret_cast<IFxMesh**>(
                reinterpret_cast<unsigned char*>(Item) + 0x30);
            Mesh->Render(reinterpret_cast<void*>(static_cast<std::uintptr_t>(G_DrawCookie)));
            Item = Item->Next;
        }
        return GR_OK;
    }

    FxParticleAppearance::FxParticleAppearance(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), ParticlePropertySet_(nullptr)
    {
    }

    FxParticleAppearance::~FxParticleAppearance()
    {
    }

    // FUN_0622f550: operator_new(0xa8) (cached at node+0xc0).
    void FxParticleAppearance::CreatePropertySet()
    {
        AdoptPropertySet(new FFxParticleAppearancePropertySet());
    }

    void FxParticleAppearance::OnPropertySetChanged()
    {
        ParticlePropertySet_ = static_cast<FFxParticleAppearancePropertySet*>(PropertySet_);
    }

    // FUN_0622f480: as basic's clone, with the set cache at +0xc0.
    GENRESULT DACOM_API FxParticleAppearance::CloneNode(IDAComponent** Out)
    {
        FxParticleAppearance* const Node = new (std::nothrow) FxParticleAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->ParticlePropertySet_ = ParticlePropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_06231930: UseDynamicRotation bit 0, SmoothRotation bit 1 of +0xa0.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::GetPropBool(unsigned long Hash,
                                                                      unsigned char* Out)
    {
        if (Hash == kParticleUseDynamicRotationHash)
        {
            *Out = static_cast<unsigned char>(Flags & kParticleFlagUseDynamicRotation);
            return GR_OK;
        }
        if (Hash == kParticleSmoothRotationHash)
        {
            *Out = static_cast<unsigned char>((Flags >> 1) & 1u);
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // FUN_06231980.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::SetPropBool(unsigned long Hash,
                                                                      unsigned char Value)
    {
        unsigned long Mask = 0;
        if (Hash == kParticleUseDynamicRotationHash)
            Mask = kParticleFlagUseDynamicRotation;
        else if (Hash == kParticleSmoothRotationHash)
            Mask = kParticleFlagSmoothRotation;
        else
            return GR_ALE_CREATE_FAIL;
        if (Value != 0)
            Flags |= Mask;
        else
            Flags &= ~Mask;
        return GR_OK;
    }

    // FUN_06231ad0; else FUN_0623f9a0.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::GetPropName(unsigned long Hash,
                                                                      const char** Out)
    {
        if (Hash == kParticleLifeNameHash)
        {
            *Out = LifeName.Buffer;
            return GR_OK;
        }
        if (Hash == kParticleDeathNameHash)
        {
            *Out = DeathName.Buffer;
            return GR_OK;
        }
        return FFxNodePropertySet::GetPropName(Hash, Out);
    }

    // FUN_06231b20; else FUN_0623f9e0.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::SetPropName(unsigned long Hash,
                                                                      const char* Text)
    {
        if (Hash == kParticleLifeNameHash)
        {
            LifeName.Assign(Text);
            return GR_OK;
        }
        if (Hash == kParticleDeathNameHash)
        {
            DeathName.Assign(Text);
            return GR_OK;
        }
        return FFxNodePropertySet::SetPropName(Hash, Text);
    }

    // +0x3c is FUN_06244330 itself (FFxAppearancePropertySet's).

    // FUN_06231cf0: FUN_06244320, then the table 0x06256780 unless it returned non-zero.
    int DACOM_API FFxParticleAppearancePropertySet::EnumerateProps(
        FNodePropertyRowCallback Callback, void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kParticlePropertyRows, Callback, Context);
    }

    // ---- FxParticleAppearance ----
    //
    // [node+0xc0] is the class's own property set (the 0xa8 object FUN_0622f550 builds and
    // FUN_0622f480 copies into every clone): +0x70/+0x74 the ParticleApp_LifeName
    // buffer/length (hash 0x0ec7a290, setter LAB_0622f270 / FUN_06231b20), +0x90/+0x94
    // ParticleApp_DeathName (0x03b76e6c), +0xa0 the UseDynamicRotation / SmoothRotation
    // bits. In the spawn two calls interleave: &child is pushed first, then
    // IAlchemy::HashNodeName(name) leaves the hash on the stack for
    // IFxEffectLibrary::CreateEffect([node+0x18], hash, &child).
    namespace
    {
        FParticleAppRecord* AsParticleRecord(FPooledParticle* Record)
        {
            return reinterpret_cast<FParticleAppRecord*>(Record);
        }

        // FUN_062306c0, used by SpawnParticle's Child and Held unwind scopes.
        struct FParticleEffectReference
        {
            FParticleEffectReference() : Value(nullptr), bReleaseOnDestruction(true) {}
            explicit FParticleEffectReference(IFxEffect* Effect)
                : Value(Effect), bReleaseOnDestruction(true)
            {
                if (Value != nullptr)
                    Value->AddRef();
            }
            ~FParticleEffectReference() noexcept(false)
            {
                if (bReleaseOnDestruction)
                    Reset();
            }
            FParticleEffectReference(const FParticleEffectReference&) = delete;
            FParticleEffectReference& operator=(const FParticleEffectReference&) = delete;

            void Reset()
            {
                // SpawnParticle disarms its unwind state before the normal Release call.
                bReleaseOnDestruction = false;
                if (Value != nullptr)
                {
                    Value->Release();
                    Value = nullptr;
                }
            }

            IFxEffect* Value;
            bool bReleaseOnDestruction;
        };

        I3DMathEngine* ParticleMath()
        {
            I3DMathEngine* Math = Shared3DMath();
            return Math;
        }

        // FUN_062307f0: preserve aliased inputs until the service returns.
        void MultiplyParticleQuaternions(Quaternion& Out, const Quaternion& A,
            const Quaternion& B)
        {
            Quaternion Product;
            ParticleMath()->MulQuat(Product, A, B);
            std::memcpy(&Out, &Product, sizeof(Product));
        }

#pragma optimize("", off)
#pragma float_control(except, on, push)
        // The Newton terms retain LengthSquared; only the integer seed sees its float store.
        __declspec(noinline) double ParticleInverseSqrt(const double& LengthSquared)
        {
            const float RoundedLengthSquared = static_cast<float>(LengthSquared);
            std::int32_t LengthBits;
            std::memcpy(&LengthBits, &RoundedLengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            return (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
        }

        // FUN_06230690 retains the complete XYZ sum through its square root.
        __declspec(noinline) double ParticleAxisLength(const float* Axis)
        {
            const double Z = Axis[2];
            const double Y = Axis[1];
            const double X = Axis[0];
            const double SquareX = X * X;
            const double SquareY = Y * Y;
            const double SumXy = SquareX + SquareY;
            const double SquareZ = Z * Z;
            const double LengthSquared = SumXy + SquareZ;
            return ComputeAlchemyFpuSquareRoot(LengthSquared);
        }

        // FUN_06206c40 calculates all three Z/Y/X products before X/Y/Z stores.
        __declspec(noinline) void ScaleParticleQuaternionVector(Quaternion& Out,
            const float* Axis, const float& Scale)
        {
            const double Z = static_cast<double>(Scale) * Axis[2];
            const double Y = static_cast<double>(Scale) * Axis[1];
            const double X = static_cast<double>(Scale) * Axis[0];
            StoreAlchemyFpuFloat(Out.x, X);
            StoreAlchemyFpuFloat(Out.y, Y);
            StoreAlchemyFpuFloat(Out.z, Z);
        }
#pragma float_control(pop)
#pragma optimize("", on)

        // About -pi/4, exactly as retail's double DAT_0624e7f0 holds it
        // (0xbfe921fb51000000 = -0.7853981573134661, read from the PE bytes).
        double ParticleTwistAngle()
        {
            const unsigned long long Bits = 0xbfe921fb51000000ull;
            double Value = 0.0;
            std::memcpy(&Value, &Bits, sizeof(Value));
            return Value;
        }

        // One sphere of FUN_0622fa10's merge, after the first: Out grows to hold Child.
        // bInlineLength selects the second child's shape (inline (y*y + z*z) + x*x,
        // FUN_06206b30 * LenSq, FUN_06206b80 add) over the first's (FUN_06206ad0 length,
        // component adds); the arithmetic is otherwise the same.
#pragma optimize("", off)
#pragma float_control(except, on, push)
        __declspec(noinline) void MergeParticleSphere(float* Out, const float* Child,
                                                       bool bInlineLength)
        {
            // FUN_06206c10 calculates Z/Y/X before the three X/Y/Z float stores.
            const double DifferenceZ = static_cast<double>(Child[2]) - Out[2];
            const double DifferenceY = static_cast<double>(Child[1]) - Out[1];
            const double DifferenceX = static_cast<double>(Child[0]) - Out[0];
            float D[3];
            StoreAlchemyFpuFloat(D[0], DifferenceX);
            StoreAlchemyFpuFloat(D[1], DifferenceY);
            StoreAlchemyFpuFloat(D[2], DifferenceZ);
            double LenSq;
            if (bInlineLength)
            {
                const double SquareY = static_cast<double>(D[1]) * D[1];
                const double SquareZ = static_cast<double>(D[2]) * D[2];
                const double SumYz = SquareY + SquareZ;
                const double SquareX = static_cast<double>(D[0]) * D[0];
                const float RoundedLength = static_cast<float>(SumYz + SquareX);
                LenSq = RoundedLength;
            }
            else
            {
                const double Z = D[2];
                const double Y = D[1];
                const double X = D[0];
                const double SquareX = X * X;
                const double SquareY = Y * Y;
                const double SumXy = SquareX + SquareY;
                const double SquareZ = Z * Z;
                LenSq = SumXy + SquareZ;
            }
            const double Dist = ParticleInverseSqrt(LenSq) * LenSq;
            const volatile float RoundedDist = static_cast<float>(Dist);
            // Containment only for an ordered less on the unrounded sum.
            if (Dist + Out[3] < Child[3])
            {
                std::memcpy(Out, Child, 4 * sizeof(float));
                return;
            }
            // Returns only for an ordered less, using the stored distance.
            if (static_cast<double>(Child[3]) + RoundedDist < Out[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Child[3]) + RoundedDist) + Out[3]) * 0.5);
            // An ordered less-or-equal skips the shift; unordered shifts.
            if (!(static_cast<double>(RoundedDist) <= 1e-06))
            {
                const double Inv = 1.0 / RoundedDist;
                StoreAlchemyFpuFloat(D[0], Inv * D[0]);
                StoreAlchemyFpuFloat(D[1], Inv * D[1]);
                StoreAlchemyFpuFloat(D[2], Inv * D[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Out[3]);
                const double ProductZ = static_cast<double>(Step) * D[2];
                const double ProductY = static_cast<double>(Step) * D[1];
                const double ProductX = static_cast<double>(Step) * D[0];
                float Offset[3];
                StoreAlchemyFpuFloat(Offset[0], ProductX);
                StoreAlchemyFpuFloat(Offset[1], ProductY);
                StoreAlchemyFpuFloat(Offset[2], ProductZ);
                StoreAlchemyFpuFloat(Out[0], static_cast<double>(Offset[0]) + Out[0]);
                StoreAlchemyFpuFloat(Out[1], static_cast<double>(Offset[1]) + Out[1]);
                StoreAlchemyFpuFloat(Out[2], static_cast<double>(Offset[2]) + Out[2]);
            }
            std::memcpy(Out + 3, &NewRadius, sizeof(NewRadius));
        }
#pragma float_control(pop)
#pragma optimize("", on)
    } // namespace

    // FUN_0622ff00: moves the record and poses its life effect (+0x6c, IFxEffect slot 3,
    // no null test).
    //   UseDynamicRotation: orient along the velocity (fast rsqrt; |N.y| < 0.99f,
    //     DAT_0624e7f8, picks the axis, otherwise a -pi/4 twist about z is applied).
    //   SmoothRotation: Speed < 0.5f slerps towards identity by (Dt + [+0x68]) + 0.01f,
    //     else by Speed * Dt, clamped to 1; +0x68 is zeroed first.
#pragma float_control(except, on, push)
    __declspec(noinline) void FxParticleAppearance::IntegrateRecord(FPooledParticle* Record,
                                                                 const float& Dt)
    {
        FParticleAppRecord* const R = AsParticleRecord(Record);
        const volatile float StepZ = static_cast<float>(static_cast<double>(Dt) * Record->VelZ);
        const volatile float StepY = static_cast<float>(static_cast<double>(Dt) * Record->VelY);
        const volatile float StepX = static_cast<float>(static_cast<double>(Dt) * Record->VelX);
        Record->PosX = static_cast<float>(static_cast<double>(StepX) + Record->PosX);
        Record->PosY = static_cast<float>(static_cast<double>(StepY) + Record->PosY);
        Record->PosZ = static_cast<float>(static_cast<double>(StepZ) + Record->PosZ);

        Transform M;
        FillIdentity3x3(&M.Rotation);
        std::memcpy(&M.Translation, &Record->PosX, 3 * sizeof(float));

        const unsigned long Flags = ParticleProps()->Flags; // [[node+0xc0]+0xa0]
        if ((Flags & kParticleFlagUseDynamicRotation) != 0)
        {
            float N[3];
            std::memcpy(N, &Record->VelX, sizeof(N));
            const float LenSqA = static_cast<float>(
                (static_cast<double>(N[2]) * N[2] + static_cast<double>(N[0]) * N[0]) +
                static_cast<double>(N[1]) * N[1]);
            const double RoundedNormalLength = LenSqA;
            const double InvLen = ParticleInverseSqrt(RoundedNormalLength);
            StoreAlchemyFpuFloat(N[0], static_cast<double>(N[0]) * InvLen);
            StoreAlchemyFpuFloat(N[1], static_cast<double>(N[1]) * InvLen);
            const double Nz = InvLen * N[2];
            const volatile double SpeedZ = Record->VelZ;
            const volatile double SpeedY = Record->VelY;
            const volatile double SpeedX = Record->VelX;
            const double SquareX = SpeedX * SpeedX;
            const double SquareY = SpeedY * SpeedY;
            const double SumXy = SquareX + SquareY;
            const double SquareZ = SpeedZ * SpeedZ;
            const double LenSqB = SumXy + SquareZ;
            const float Speed = static_cast<float>(ParticleInverseSqrt(LenSqB) * LenSqB);

            Quaternion Q;
            // >= 0.99 or unordered takes the other axis.
            if (std::fabs(static_cast<double>(N[1])) < 0.9900000095367432)
            {
                const float Axis[3] = {static_cast<float>(Nz), 0.0f, -N[0]};
                const double Half = std::acos(static_cast<double>(N[1])) * 0.5;
                const volatile float StoredHalf = static_cast<float>(Half);
                const float Sine = static_cast<float>(ComputeAlchemyFpuSine(Half));
                const float Scale = static_cast<float>(static_cast<double>(Sine) / ParticleAxisLength(Axis));
                const double RoundedHalf = StoredHalf;
                const float Cosine = static_cast<float>(ComputeAlchemyFpuCosine(RoundedHalf));
                std::memcpy(&Q.w, &Cosine, sizeof(Cosine));
                ScaleParticleQuaternionVector(Q, Axis, Scale);
            }
            else
            {
                const float Axis[3] = {0.0f, static_cast<float>(-Nz), N[1]};
                const double Half = std::acos(static_cast<double>(N[0])) * 0.5;
                const volatile float StoredHalf = static_cast<float>(Half);
                const float Sine = static_cast<float>(ComputeAlchemyFpuSine(Half));
                const float Scale = static_cast<float>(static_cast<double>(Sine) / ParticleAxisLength(Axis));
                Quaternion Q1;
                const double RoundedHalf = StoredHalf;
                const float Cosine = static_cast<float>(ComputeAlchemyFpuCosine(RoundedHalf));
                std::memcpy(&Q1.w, &Cosine, sizeof(Cosine));
                ScaleParticleQuaternionVector(Q1, Axis, Scale);
                const float TwistAxis[3] = {0.0f, 0.0f, 1.0f};
                const double Twist = ParticleTwistAngle();
                const float TwistSine = static_cast<float>(ComputeAlchemyFpuSine(Twist));
                const float TwistScale = static_cast<float>(
                    static_cast<double>(TwistSine) / ParticleAxisLength(TwistAxis));
                Quaternion Q2;
                const float TwistCosine = static_cast<float>(ComputeAlchemyFpuCosine(Twist));
                std::memcpy(&Q2.w, &TwistCosine, sizeof(TwistCosine));
                ScaleParticleQuaternionVector(Q2, TwistAxis, TwistScale);
                MultiplyParticleQuaternions(Q, Q1, Q2);
            }
            std::memcpy(&R->VelQuat, &Q, sizeof(Q));
            R->Blend = 0.0f;

            Matrix Rotation;
            // Retail reloads the class cache after the MulQuat callback.
            if ((ParticleProps()->Flags & kParticleFlagSmoothRotation) != 0)
            {
                // Speed >= 0.5 (or unordered) takes Speed * Dt.
                if (Speed < 0.5f)
                {
                    R->VelQuat.w = 1.0f;
                    R->VelQuat.x = 0.0f;
                    R->VelQuat.y = 0.0f;
                    R->VelQuat.z = 0.0f;
                    R->Blend = (static_cast<double>(Dt) + R->Blend) +
                        static_cast<double>(FloatFromBits(0x3c23d70au)); // DAT_0624c350
                }
                else
                {
                    R->Blend = static_cast<double>(Speed) * Dt;
                }
                if (R->Blend > 1.0f)
                    R->Blend = 1.0f;
                // Capture the raw interpolation word before lazy creation.
                float SlerpWeight;
                std::memcpy(&SlerpWeight, &R->Blend, sizeof(SlerpWeight));
                Quaternion Slerped;
                ParticleMath()->QuatSlerpValue(Slerped,
                    R->BlendQuat,
                    R->VelQuat, &SlerpWeight);
                std::memcpy(&R->BlendQuat, &Slerped, sizeof(Slerped));
                ParticleMath()->QuaternionToMatrix(Rotation,
                    R->BlendQuat); // +0x54
            }
            else
            {
                ParticleMath()->QuaternionToMatrix(Rotation,
                    R->VelQuat); // +0x54
            }
            std::memcpy(&M, &Rotation, sizeof(float) * 9);
        }

        R->LifeEffect->SetTransform(1, &M);
    }
#pragma float_control(pop)

    // FUN_062306e0 (the list, &next, record): FUN_062308c0 unlinks and yields the next
    // record, FUN_06230600's teardown runs on +0x6c/+0x70 (FreePoolItem's particle branch)
    // and FUN_0623ecf0 returns the record to [list+8] = G_PoolParticle.
    FPooledParticle* FxParticleAppearance::EraseRecord(FPooledParticle* Record)
    {
        FPooledParticle* const Next = Record->Next;
        UnlinkParticleNode(&InstanceListHead_, &InstanceListTail_, Record);
        ReleaseAppearancePoolItem(&G_PoolParticle, Record);
        return Next;
    }

    // FUN_0622f6a0, always returns 0. A full pool (DAT_06257ad4 == 0) evicts the oldest
    // record through its owner (FUN_0622f880 -> FUN_062306e0). An empty LifeName or no
    // IAlchemy (DAT_06257b98) spawns nothing; DAT_06257b90 (the effect library) is not
    // tested. FUN_06231470 appends the record even when the allocation failed. The child
    // gets slot 6 / slot 14 only when node+0x8c != 0 / node+0x90 != 1.
    GENRESULT DACOM_API FxParticleAppearance::SpawnParticle(const float* Six, float Age, float Duration)
    {
        if (G_PoolParticle.FreeCount == 0)
        {
            FPooledParticle* const Oldest =
                ParticleFromUsedLink(&G_PoolParticle, G_PoolParticle.UsedHead);
            FxParticleAppearance* const Owner = static_cast<FxParticleAppearance*>(
                static_cast<FGenericFxObjectBase*>(Oldest->Appearance));
            Owner->EraseRecord(Oldest);
        }

        const FFxHashedName& LifeNameString = ParticleProps()->LifeName;
        if (LifeNameString.Length == 0)
            return GR_OK;
        const char* const LifeName = LifeNameString.Buffer;

        FParticleEffectReference Child;
        if (G_RuntimeAlchemy != nullptr)
        {
            IAlchemy* const Alchemy = static_cast<IAlchemy*>(G_RuntimeAlchemy);
            IFxEffectLibrary* const Library = static_cast<IFxEffectLibrary*>(G_RuntimeEffectLibrary);
            const unsigned long Key = Alchemy->HashNodeName(LifeName);
            if (Library->CreateEffect(static_cast<IGenericFx*>(this),
                                      reinterpret_cast<void*>(static_cast<std::uintptr_t>(Key)),
                                      &Child.Value) == GR_OK)
            {
                FParticleEffectReference Held(Child.Value);
                float CapturedSix[6];
                std::memcpy(CapturedSix, Six, sizeof(CapturedSix));

                FParticleAppRecord* const Record =
                    static_cast<FParticleAppRecord*>(AllocPoolItem(&G_PoolParticle));
                if (Record != nullptr)
                {
                    // FUN_06231d30.
                    Record->Base.Appearance = static_cast<FGenericFxObjectBase*>(this);
                    std::memcpy(&Record->Base.PosX, CapturedSix, sizeof(CapturedSix));
                    std::memcpy(&Record->Base.Duration, &Duration, sizeof(Duration));
                    std::memcpy(&Record->Base.Age, &Age, sizeof(Age));
                    Record->LifeEffect = Held.Value;
                    if (Held.Value != nullptr)
                        Held.Value->AddRef();
                    InitializeParticleReciprocal(*Record);
                }
                AppendParticleNode(&InstanceListHead_, &InstanceListTail_,
                                   Record != nullptr ? &Record->Base : nullptr);
                Held.Reset();

                FParticleAppRecord* const Tail = AsParticleRecord(InstanceListTail_);
                Tail->UnusedSpawnWords_0x30[0] = 0;
                Tail->UnusedSpawnWords_0x30[1] = 0x3f800000u;
                Tail->UnusedSpawnWords_0x30[2] = 0;
                Tail->UnusedSpawnWords_0x4c[0] = 0x3f800000u;
                Tail->UnusedSpawnWords_0x4c[1] = 0;
                Tail->UnusedSpawnWords_0x4c[2] = 0;
                Tail->BlendQuat.w = 1.0f;
                Tail->BlendQuat.z = 0.0f;
                Tail->BlendQuat.y = 0.0f;
                Tail->BlendQuat.x = 0.0f;
                Tail->VelQuat.w = 1.0f;
                Tail->VelQuat.z = 0.0f;
                Tail->VelQuat.y = 0.0f;
                Tail->VelQuat.x = 0.0f;
                Tail->Blend = 0.0f;
                IntegrateRecord(InstanceListTail_, 0.0f);

                if (IsNonzeroParticleField(AppearanceOuterTime_))
                    ForwardEffectFloat(Child.Value, &IFxEffect::SetOuterTime, AppearanceOuterTime_);
                if (IsNonunitParticleField(LodInput_))
                    ForwardEffectFloat(Child.Value, &IFxEffect::SetLodInput, LodInput_);
            }
        }
        return GR_OK;
    }
    // FUN_06230370, always returns 0. Fields apply only to records with Age < Duration.
    // A record that just ran out stops its life effect (slot 13(1)) unless slot 11(7)
    // reports it finished; an expired record is erased (FUN_062306e0) only once both its
    // life and death effects report finished.
    GENRESULT DACOM_API FxParticleAppearance::UpdateNode(float Dt)
    {
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= kNodeFlagLifetimeOver;
        ApplyFieldsToPool(this, true);

        FPooledParticle* Record = InstanceListHead_;
        while (Record != nullptr)
        {
            FParticleAppRecord* const R = AsParticleRecord(Record);
            if (Record->Age <= Record->Duration)
            {
                Record->Age = Dt + Record->Age;
                if (!(Record->Age > Record->Duration))
                    IntegrateRecord(Record, Dt);
                else if (static_cast<unsigned char>(
                             R->LifeEffect->IsFinished(reinterpret_cast<void*>(7))) == 0)
                    R->LifeEffect->DestroyNodes(reinterpret_cast<void*>(1));
                Record = Record->Next;
                continue;
            }
            if (static_cast<unsigned char>(R->LifeEffect->IsFinished(reinterpret_cast<void*>(7))) == 0)
            {
                Record = Record->Next;
                continue;
            }
            if (R->DeathEffect != nullptr &&
                static_cast<unsigned char>(R->DeathEffect->IsFinished(reinterpret_cast<void*>(7))) == 0)
            {
                Record = Record->Next;
                continue;
            }
            Record = EraseRecord(Record);
        }
        return GR_OK;
    }

    // FUN_0622fcf0: spawns the death effect (+0x70) of each record strictly past its
    // Duration (ordered) that has none yet, when DeathName is set and both DAT_06257b90 and
    // DAT_06257b98 are bound; then the shared compose FUN_062401a0.
    GENRESULT DACOM_API FxParticleAppearance::ComposeNodeTransform(void* Unused,
                                                                   const Transform* Parent)
    {
        if (ParticleProps()->DeathName.Length != 0)
        {
            FPooledParticle* Record = InstanceListHead_;
            while (Record != nullptr)
            {
                FParticleAppRecord* const R = AsParticleRecord(Record);
                if (R->DeathEffect == nullptr && Record->Age > Record->Duration &&
                    G_RuntimeEffectLibrary != nullptr && G_RuntimeAlchemy != nullptr)
                {
                    IAlchemy* const Alchemy = static_cast<IAlchemy*>(G_RuntimeAlchemy);
                    IFxEffectLibrary* const Library =
                        static_cast<IFxEffectLibrary*>(G_RuntimeEffectLibrary);
                    const unsigned long Key = Alchemy->HashNodeName(ParticleProps()->DeathName.Buffer);
                    if (Library->CreateEffect(
                            static_cast<IGenericFx*>(this),
                            reinterpret_cast<void*>(static_cast<std::uintptr_t>(Key)),
                            &R->DeathEffect) == GR_OK)
                    {
                        Transform M = {Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                                       {0.0f, 0.0f, 0.0f}};
                        // Position dwords copied without conversion.
                        std::memcpy(&M.Translation, &Record->PosX, 3 * sizeof(float));
                        R->DeathEffect->SetTransform(1, &M);
                        if (IsNonzeroParticleField(AppearanceOuterTime_))
                            ForwardEffectFloat(R->DeathEffect, &IFxEffect::SetOuterTime,
                                               AppearanceOuterTime_);
                        if (IsNonunitParticleField(LodInput_))
                            ForwardEffectFloat(R->DeathEffect, &IFxEffect::SetLodInput, LodInput_);
                    }
                }
                Record = Record->Next;
            }
        }
        return FGenericFxObjectBase::ComposeNodeTransform(Unused, Parent);
    }
    // FUN_0622f8b0: node+0x8c, then the original argument to each life/death effect
    // (IFxEffect slot 6).
    GENRESULT DACOM_API FxParticleAppearance::SetNodeOuterTime(float OuterTime)
    {
        AppearanceOuterTime_ = OuterTime;
        ForwardChildFloat(&IFxEffect::SetOuterTime, OuterTime);
        return GR_OK;
    }

    // FUN_0622f960: node+0x90, then slot 14 on each child with the original argument.
    GENRESULT DACOM_API FxParticleAppearance::SetNodeLodInput(float LodInput)
    {
        LodInput_ = LodInput;
        ForwardChildFloat(&IFxEffect::SetLodInput, LodInput);
        return GR_OK;
    }

    // FUN_0622fa10: every child effect's sphere (IFxEffect slot 15) that answers 0 is
    // merged (MergeParticleSphere); none at all -> -1.
    GENRESULT DACOM_API FxParticleAppearance::GetBoundingSphere(float* OutSphere)
    {
        unsigned long Merged = 0;
        FPooledParticle* Record = InstanceListHead_;
        while (Record != nullptr)
        {
            FParticleAppRecord* const R = AsParticleRecord(Record);
            float Child[4];
            if (R->LifeEffect != nullptr && R->LifeEffect->GetBoundingSphere(Child) == GR_OK)
            {
                if (Merged == 0)
                {
                    std::memcpy(OutSphere, Child, sizeof(Child));
                }
                else
                {
                    MergeParticleSphere(OutSphere, Child, false);
                }
                ++Merged;
            }
            if (R->DeathEffect != nullptr && R->DeathEffect->GetBoundingSphere(Child) == GR_OK)
            {
                if (Merged == 0)
                {
                    std::memcpy(OutSphere, Child, sizeof(Child));
                }
                else
                {
                    MergeParticleSphere(OutSphere, Child, true);
                }
                ++Merged;
            }
            Record = Record->Next;
        }
        return Merged == 0 ? GR_GENERIC : GR_OK;
    }

} // namespace AlchemyImpl
