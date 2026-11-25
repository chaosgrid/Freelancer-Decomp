// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/cli_tractor_beam.h"

#include "Space/Effects/client_equip_fx.h"   // FClientEquip
#include "Space/Effects/effect_type_list.h"  // EffectTypeListLink (FUN_004f7af0)
#include "Space/Cameras/space_camera.h"      // FSpaceCamera
#include "Common/Graphics/texture_shapes.h"    // G_EffectShapeDB (DAT_00674f24)
#include "Interface/win3d_immediate.h"   // Win3DDrawFuzzyLine (FUN_00423be0), state setters
#include "Interface/win3d_render.h"      // Win3DApplyCamera (FUN_00422950)

#include "common_cequip.h"     // CETractor, CEquip::IsFunctioning
#include "common_archetype.h"  // Archetype::Tractor::MaxLength (+0x70)
#include "common_cobject.h"    // CLoot
#include "common_csys.h"       // LookMatrixYup, Acquire3DMathEngine (FUN_005b73e0)
#include "x86math.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace
{
    // DAT_005c7f68 (0x3b808081): 1/255.
    constexpr float kByteToUnit = 1.0f / 255.0f;

    // The tractor effect's type bucket, EFT_TRACTORBEAM.
    constexpr unsigned kTractorBeamEffectType = 0x2du;

    // LAB_00512da0's constants: DAT_005cf100 (0.2 as a double), DAT_005db708 (0.8 pi as a
    // double), DAT_005d3e08 (0.3f); the rest are immediates.
    constexpr float kLineJitterScale = 0.2f;
    constexpr float kBeamWidth = 5.0f;
    constexpr double kLootRadiusScale = 0.2;
    constexpr float kEndJitter = 0.5f;
    constexpr double kSparkSpreadScale = 2.5132741928100586;
    constexpr float kSparkPullBack = 0.3f;
    constexpr float kTwoPi = 6.2831855f;

    // DAT_005da778: 1/32767 as a float.
    constexpr float kRandUnit = 1.0f / 32767.0f;

    // DAT_006751b0 -- the beam draw's 3DMathEngine, acquired on first use.
    I3DMathEngine* G_TractorBeamMath = nullptr;

    // FUN_0053ebe0: `rand() * Scale * DAT_005da778`, unrounded. Its only callers are in
    // LAB_00512da0.
    double RandomScaled(float Scale)
    {
        return static_cast<double>(rand()) * Scale * kRandUnit;
    }

    // [[this+0x6c]+4]: the wrapper's equipment, used as the CETractor without a cast.
    CETractor* BeamTractor(FCliTractorBeam const* Self)
    {
        return static_cast<CETractor*>(Self->Owner->Equip);
    }
}

float FCliTractorBeam::GetWorldPos(Vector* Out)
{
    FCliTractorBeam* const Self = this;
    // LAB_005133b0: the source position; the radius is the arch's max_length (+0x70).
    CETractor* const Tractor = BeamTractor(Self);
    Vector const Source = Tractor->GetSourcePos();
    Out->x = Source.x;
    Out->y = Source.y;
    Out->z = Source.z;
    return Tractor->TractorArch()->MaxLength;
}

unsigned char FCliTractorBeam::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FCliTractorBeam* const Self = this;
    // LAB_005133f0: FUN_004f7b50 with the camera only.
    return EffectInCameraView(Self, Cam);
}

unsigned char FCliTractorBeam::BuildGeometry(FSpaceCamera* Cam)
{
    FCliTractorBeam* const Self = this;
    // LAB_00512da0.
    CETractor* const Tractor = BeamTractor(Self);
    if (!Tractor->IsFunctioning())
        return 0;

    // The line record's fixed fields (the "line" texture without a null test). Retail leaves
    // the rest of its stack record unwritten until each draw fills it; the port
    // value-initialises it (no field is read before it is written).
    Vector const Source = Tractor->GetSourcePos();
    FWin3DFuzzyLine Line = {};
    Line.Color[0] = Self->Color[0];
    Line.Color[1] = Self->Color[1];
    Line.Color[2] = Self->Color[2];
    Line.Texture = Self->LineShape->Texture;
    Line.JitterScale = kLineJitterScale;
    const Vector* const Eye = AsCamera(Cam)->GetPosition();
    Line.EyePos[0] = Eye->x;
    Line.EyePos[1] = Eye->y;
    Line.EyePos[2] = Eye->z;
    Line.SideScale = 1.0f;

    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DCullFace);
    Win3DApplyCamera(AsCamera(Cam));

    // The drawn-arm count is tested before every arm (> 10), so at most eleven are drawn.
    std::vector<TractorArm> const& Arms = Tractor->GetTractorArms();
    int Drawn = 0;
    for (size_t ArmIndex = 0; ArmIndex < Arms.size(); ++ArmIndex)
    {
        if (Drawn > 10)
            break;
        TractorArm const& Arm = Arms[ArmIndex];

        // The watcher's Watchable and the CLoot 0x9c before it (GetTarget inlined); either null
        // skips the arm without counting it.
        if (Arm.TargetWatch.target() == nullptr)
            continue;
        CLoot* const Loot = Arm.GetTarget();
        if (Loot == nullptr)
            continue;

        // The source -> tip distance (rounded to float) and the unit direction.
        Vector const Tip = Arm.GetTipPos();
        float const ToTip[3] = {Tip.x - Source.x, Tip.y - Source.y, Tip.z - Source.z};
        float const TipDistance = static_cast<float>(
            sqrt(static_cast<double>(ToTip[1]) * ToTip[1] +
                 static_cast<double>(ToTip[2]) * ToTip[2] +
                 static_cast<double>(ToTip[0]) * ToTip[0]));
        double const InvTipDistance = 1.0 / TipDistance;
        float const TipDir[3] = {static_cast<float>(ToTip[0] * InvTipDistance),
                                 static_cast<float>(ToTip[1] * InvTipDistance),
                                 static_cast<float>(ToTip[2] * InvTipDistance)};

        Vector const LootPos = Loot->WorldPosition;
        float const ToLoot[3] = {LootPos.x - Source.x, LootPos.y - Source.y,
                                 LootPos.z - Source.z};
        float const LootDistance = static_cast<float>(
            sqrt(static_cast<double>(ToLoot[1]) * ToLoot[1] +
                 static_cast<double>(ToLoot[2]) * ToLoot[2] +
                 static_cast<double>(ToLoot[0]) * ToLoot[0]));

        // Reach = distance to the loot's shell (0.2 x its physical radius), Excess = how far the
        // tip is beyond it.
        Vector RadiusCentre;
        float const Radius =
            static_cast<float>(Loot->get_physical_radius_r(RadiusCentre) * kLootRadiusScale);
        float const Reach = LootDistance - Radius;
        float EndPoint[3] = {Tip.x, Tip.y, Tip.z};
        float const Excess = TipDistance - Reach;

        // Strictly past the shell (not NaN) the end stops on it -- unscaled when |ToLoot|^2 is
        // exactly 0. z keeps its product unrounded into the sum; x and y are rounded first.
        if (Excess > 0.0f)
        {
            double const LootDistanceSq = static_cast<double>(ToLoot[1]) * ToLoot[1] +
                                          static_cast<double>(ToLoot[2]) * ToLoot[2] +
                                          static_cast<double>(ToLoot[0]) * ToLoot[0];
            float ShellX = ToLoot[0];
            float ShellY = ToLoot[1];
            double ShellZ = ToLoot[2];
            if (LootDistanceSq != 0.0)
            {
                double const Scale = Reach / sqrt(LootDistanceSq);
                ShellX = static_cast<float>(ToLoot[0] * Scale);
                ShellY = static_cast<float>(ToLoot[1] * Scale);
                ShellZ = ToLoot[2] * Scale;
            }
            EndPoint[0] = ShellX + Source.x;
            EndPoint[1] = ShellY + Source.y;
            EndPoint[2] = static_cast<float>(ShellZ + Source.z);
        }

        // Two draws: the first (rounded) moves y, the second (unrounded) moves x.
        float const JitterY = static_cast<float>(RandomScaled(kEndJitter));
        double const JitterX = RandomScaled(kEndJitter);
        EndPoint[0] = static_cast<float>(EndPoint[0] + JitterX);
        EndPoint[1] = EndPoint[1] + JitterY;

        // The straight beam, source -> end.
        Line.bCurved = 0;
        Line.Start[0] = Source.x;
        Line.Start[1] = Source.y;
        Line.Start[2] = Source.z;
        Line.End[0] = EndPoint[0];
        Line.End[1] = EndPoint[1];
        Line.End[2] = EndPoint[2];
        Line.Width = kBeamWidth;
        Win3DDrawFuzzyLine(&Line);

        // The same Excess test again for the sparks.
        if (Excess > 0.0f)
        {
            // The spread Excess / Radius * 0.8 pi.
            float const Spread = static_cast<float>(static_cast<double>(Excess) / Radius *
                                                    kSparkSpreadScale);
            Vector LookDir;
            LookDir.x = ToLoot[0];
            LookDir.y = ToLoot[1];
            LookDir.z = ToLoot[2];
            Matrix const Look = LookMatrixYup(LookDir);
            Matrix LookMatrix;
            for (int Row = 0; Row < 3; ++Row)
            {
                for (int Col = 0; Col < 3; ++Col)
                    LookMatrix.E[Row][Col] = Look.E[Row][Col];
            }

            // The spark centre, 0.3f back along the beam from the loot; x and y subtract the
            // unrounded product, z the rounded one.
            float const Centre[3] = {
                static_cast<float>(LootPos.x - static_cast<double>(TipDir[0]) * kSparkPullBack),
                static_cast<float>(LootPos.y - static_cast<double>(TipDir[1]) * kSparkPullBack),
                LootPos.z - TipDir[2] * kSparkPullBack};

            // sin kept as a double, cos rounded to float.
            double const SinSpread = sin(static_cast<double>(Spread));
            float const CosSpread = static_cast<float>(cos(static_cast<double>(Spread)));

            for (int Spark = 0; Spark < 5; ++Spark)
            {
                // A direction on the cone about the look axis.
                double const Theta = RandomScaled(kTwoPi);
                Vector ConeDir;
                ConeDir.x = static_cast<float>(cos(Theta) * SinSpread);
                ConeDir.y = static_cast<float>(sin(Theta) * SinSpread);
                ConeDir.z = CosSpread;

                if (G_TractorBeamMath == nullptr)
                    G_TractorBeamMath = Acquire3DMathEngine();
                Vector Local;
                G_TractorBeamMath->TransformMatrix(Local, LookMatrix, ConeDir);

                // The arc from the beam end to Centre + Local, bent about Centre.
                Line.End[0] = Local.x + Centre[0];
                Line.End[1] = Local.y + Centre[1];
                Line.End[2] = Local.z + Centre[2];
                Line.Control[0] = Centre[0];
                Line.Control[1] = Centre[1];
                Line.Control[2] = Centre[2];
                Line.Start[0] = EndPoint[0];
                Line.Start[1] = EndPoint[1];
                Line.Start[2] = EndPoint[2];
                Line.Width = 1.0f;
                Line.bCurved = 1;
                Win3DDrawFuzzyLine(&Line);
            }
        }

        ++Drawn;
    }

    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DCullFace);
    Win3DDisable(kWin3DBlend);
    return 1;
}

FCliTractorBeam::FCliTractorBeam() : FEffectBase(kTractorBeamEffectType)
{
    Owner = nullptr;
    Color[0] = 0.0f;
    Color[1] = 0.0f;
    Color[2] = 0.0f;
    LineShape = nullptr;
    BulbShape = nullptr;
}

FCliTractorBeam* CliTractorBeamCreate()
{
    // Inlined in the tractor's equip FX (0x0052fa2b).
    FCliTractorBeam* const Beam = static_cast<FCliTractorBeam*>(EffectAlloc(0x84u));
    if (Beam == nullptr)
        return nullptr;
    ::new (Beam) FCliTractorBeam();
    return Beam;
}

unsigned char CliTractorBeamInit(FCliTractorBeam* This, FClientEquip* Owner)
{
    // FUN_00512be0.
    This->Owner = Owner;

    // The CliEQTractor companion's colour bytes +0x54..+0x56 (no tests), scaled by 1/255.
    unsigned char const* const Comp =
        reinterpret_cast<unsigned char const*>(Owner->Equip->Arch->Companion);
    This->Color[0] = static_cast<float>(Comp[0x54]) * kByteToUnit;
    This->Color[1] = static_cast<float>(Comp[0x55]) * kByteToUnit;
    This->Color[2] = static_cast<float>(Comp[0x56]) * kByteToUnit;

    // The inlined ShapeDB find FDUMPs a miss (cmn\ShapeDB.h line 0xf2).
    This->LineShape = G_EffectShapeDB.Find("line");
    if (This->LineShape == nullptr)
        return 0;

    This->BulbShape = G_EffectShapeDB.Find("bulb");
    if (This->BulbShape == nullptr)
        return 0;

    EffectTypeListLink(This);
    return 1;
}
