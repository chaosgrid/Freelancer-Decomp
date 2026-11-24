// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"
#include "x87_trig.h"     // X87SinF / X87CosF (PhySys_UpdateJointedChildren)

using namespace CmnPhySys;

// ---- Collision globals (defined here, declared in common_goods.h) ----
extern float COLLISION_DAMAGE_FACTOR;

namespace CmnPhySys
{
    // DAT_063fc0bc: the game's single IVP material. PhySys::Startup creates it once; Shutdown
    // deletes it when the world map is empty. The construction templates' default material.
    IVP_Material* G_PhySysMaterial = nullptr;
}

namespace
{
    // ---- Collision damage table ----
    // DAT_063fcc58, a std::map<unsigned, float> (not multi): DecodeType(name) -> damage.
    std::map<unsigned, float> G_CollisionDamage;
    // DAT_063ed178; written only by Collision::Load.
    float G_CollisionMaxImpactSpeed = 250.0f;

    // ---- Default Part: DAT_063fc02c, see GetDefaultPart ----
    PhyArch::Part G_DefaultPartStorage;
}

// ============================================================================
// PhySys constants -- read by common_constants.cpp ReadConstants.
// ============================================================================
namespace PhySys
{
    float BULLET_LENGTH_FUDGE = 0.0f;
    FCollisionFilter m_pCollisionFilter = nullptr;

    // Retail defaults: the scalars from .data (063eb360..063eb38c); the vectors are seeded by
    // the static initializers LAB_062908d0 / LAB_062908f0 / LAB_06290910.
    float DEFAULT_LINEAR_DAMPING = 1.0f;
    Vector DEFAULT_ANGULAR_DAMPING = {2.0f, 2.0f, 2.0f};
    float GOLEM_CHILD_MASS = 100000.0f;
    float GOLEM_CHILD_LINEAR_DAMP = 1.0f;
    Vector GOLEM_CHILD_ANGULAR_DAMP = {1.0f, 1.0f, 1.0f};
    float GOLEM_MAX_DELTA_POSITION = 10.0f;
    float GOLEM_DELTA_ORIENTATION = 1.5707964f;
    Vector GOLEM_MAX_TRANSLATION_FORCE = {1.0e12f, 1.0e12f, 1.0e12f};
    float GOLEM_MAX_TORQUE = 1.0E+12f;
    float GOLEM_FORCE_FACTOR = 0.8f;
    float GOLEM_DAMP_FACTOR = 1.0f;
    float GOLEM_TORQUE_FACTOR = 0.8f;
    float GOLEM_ANGULAR_DAMP_FACTOR = 1.0f;
    float RC_MAX_DELTA_POSITION = 50.0f;
    float RC_MAX_DELTA_ORIENTATION = 5.0f;
}

namespace CmnPhySys
{
    // DAT_063fc098: the world map (PhySys::AnyActive tests its size).
    std::map<unsigned, PhysicalSystem> G_PhysicalSystems;
}

namespace
{
    // The engine's per-instance joint block (IEngine::GetInstanceRelative, +0xc4) as
    // FUN_062883c0 reads it. common_cattachment.cpp's file-local FRevoluteJoint is the same block.
    struct FPhySysJointBlock
    {
        unsigned long Type;  // +0x00  0, 1 or 2 (anything else: the pose is left as the parent's)
        FVector ParentPoint; // +0x04
        FVector ChildPoint;  // +0x10
        FMatrix Orient;      // +0x1c
        FVector Axis;        // +0x40
    };
    static_assert(offsetof(FPhySysJointBlock, ParentPoint) == 0x04, "joint block +0x04");
    static_assert(offsetof(FPhySysJointBlock, ChildPoint) == 0x10, "joint block +0x10");
    static_assert(offsetof(FPhySysJointBlock, Orient) == 0x1c, "joint block +0x1c");
    static_assert(offsetof(FPhySysJointBlock, Axis) == 0x40, "joint block +0x40");
    static_assert(sizeof(FTransform) == 0x30, "the engine instance transform is 12 floats");

    // FUN_06288860 (thiscall on M): M = M * B through the math engine.
    void PhySys_MatrixMulInPlace(FMatrix& M, FMatrix const& B)
    {
        FMatrix Product;
        Acquire3DMathEngine()->MulMatrix(Product, M, B);
        M = Product;
    }

    // FUN_06288900 (cdecl): Out = A * B (+0x20).
    FMatrix PhySys_MatrixMul(FMatrix const& A, FMatrix const& B)
    {
        FMatrix Product;
        Acquire3DMathEngine()->MulMatrix(Product, A, B);
        return Product;
    }

    // FUN_062888b0 (cdecl): Out = M * V (TransformMatrix, +0x2c).
    FVector PhySys_MatrixTransform(FMatrix const& M, FVector const& V)
    {
        FVector Out;
        Acquire3DMathEngine()->TransformMatrix(Out, M, V);
        return Out;
    }
}

namespace CmnPhySys
{
    // FUN_062883c0 (cdecl): re-pose every child instance of Instance whose user data is Object
    // from the child's joint block and joint parameter, then recurse into it. The vector
    // helpers are plain float arithmetic (one rounding per component).
    void PhySys_UpdateJointedChildren(long Instance, CObject* Object)
    {
        if (Instance == -1)
            return;

        IEngine* const Engine = DALib::Engine;

        // engbase.h types the result float*; it is the instance's 12-float FTransform.
        FTransform const* const ParentTransform =
            (FTransform const*)Engine->GetInstanceTransform(PhySys_InstanceHandle(Instance));

        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            if (PhySys_InstanceObject(Child) != Object)
                continue;

            // Called once with the result unused, as in retail.
            Engine->GetInstanceRelative(PhySys_InstanceHandle(Child));

            // GetInstanceRelative hands back the child's joint block (typed unsigned long*).
            FTransform Local = *ParentTransform;
            FPhySysJointBlock const* const Joint =
                (FPhySysJointBlock const*)Engine->GetInstanceRelative(PhySys_InstanceHandle(Child));
            FMatrix const ParentRotation = Local.Rotation;

            if (Joint->Type == 0)
            {
                // R = R * Orient; T += ParentR * ChildPoint.
                PhySys_MatrixMulInPlace(Local.Rotation, Joint->Orient);
                FVector const Offset = PhySys_MatrixTransform(ParentRotation, Joint->ChildPoint);
                Local.Translation.X = Local.Translation.X + Offset.X;
                Local.Translation.Y = Local.Translation.Y + Offset.Y;
                Local.Translation.Z = Local.Translation.Z + Offset.Z;
            }
            else if (Joint->Type == 1)
            {
                // Prismatic: the joint parameter slides along Axis.
                float Parameter;
                Engine->GetJointParams(PhySys_InstanceHandle(Child), 0, &Parameter);

                PhySys_MatrixMulInPlace(Local.Rotation, Joint->Orient);
                FVector const ChildOffset = PhySys_MatrixTransform(Local.Rotation, Joint->ChildPoint);

                // Slid = ParentPoint + Axis * Parameter, each product rounded first.
                FVector Scaled;
                Scaled.X = Joint->Axis.X * Parameter;
                Scaled.Y = Joint->Axis.Y * Parameter;
                Scaled.Z = Joint->Axis.Z * Parameter;
                FVector Slid;
                Slid.X = Joint->ParentPoint.X + Scaled.X;
                Slid.Y = Joint->ParentPoint.Y + Scaled.Y;
                Slid.Z = Joint->ParentPoint.Z + Scaled.Z;

                // T += (ParentR * Slid) - ChildOffset.
                FVector const ParentOffset = PhySys_MatrixTransform(ParentRotation, Slid);
                FVector Delta;
                Delta.X = ParentOffset.X - ChildOffset.X;
                Delta.Y = ParentOffset.Y - ChildOffset.Y;
                Delta.Z = ParentOffset.Z - ChildOffset.Z;
                Local.Translation.X = Local.Translation.X + Delta.X;
                Local.Translation.Y = Local.Translation.Y + Delta.Y;
                Local.Translation.Z = Local.Translation.Z + Delta.Z;
            }
            else if (Joint->Type == 2)
            {
                // Revolute: the joint angle about Axis. x87: sin of the half angle is stored as
                // a float, divided by the unrounded axis length.
                float Angle;
                Engine->GetJointParams(PhySys_InstanceHandle(Child), 0, &Angle);

                float const HalfAngle = Angle * 0.5f;
                float const SinHalf = X87SinF(HalfAngle);
                double const AxisLength = sqrt((double)(Joint->Axis.X) * Joint->Axis.X +
                                               (double)(Joint->Axis.Y) * Joint->Axis.Y +
                                               (double)(Joint->Axis.Z) * Joint->Axis.Z);
                float const AxisScale = (float)((double)(SinHalf) / AxisLength);

                FQuaternion Rotation;
                Rotation.W = X87CosF(HalfAngle);
                Rotation.X = Joint->Axis.X * AxisScale;
                Rotation.Y = Joint->Axis.Y * AxisScale;
                Rotation.Z = Joint->Axis.Z * AxisScale;

                // R = R * (JointR * Orient).
                FMatrix JointRotation;
                Acquire3DMathEngine()->QuaternionToMatrix(JointRotation, Rotation);
                PhySys_MatrixMulInPlace(Local.Rotation, PhySys_MatrixMul(JointRotation, Joint->Orient));

                // T += (ParentR * ParentPoint) - (R * ChildPoint).
                FVector const ChildOffset = PhySys_MatrixTransform(Local.Rotation, Joint->ChildPoint);
                FVector const ParentOffset = PhySys_MatrixTransform(ParentRotation, Joint->ParentPoint);
                FVector Delta;
                Delta.X = ParentOffset.X - ChildOffset.X;
                Delta.Y = ParentOffset.Y - ChildOffset.Y;
                Delta.Z = ParentOffset.Z - ChildOffset.Z;
                Local.Translation.X = Local.Translation.X + Delta.X;
                Local.Translation.Y = Local.Translation.Y + Delta.Y;
                Local.Translation.Z = Local.Translation.Z + Delta.Z;
            }

            // Other joint types keep the parent's pose.
            Engine->SetInstanceMatrix(PhySys_InstanceHandle(Child), &Local);

            // Read the instance extent (flags 1) and write it back.
            float Extent;
            FVector ExtentVector;
            Engine->GetInstanceExtent(PhySys_InstanceHandle(Child), 1, &Extent, &ExtentVector.X);
            Engine->SetInstanceExtent(PhySys_InstanceHandle(Child), 1, Extent, &ExtentVector.X);

            PhySys_UpdateJointedChildren(Child, Object);
        }
    }
}

// ============================================================================
// Collision namespace
// ============================================================================

// FUN_06302940. Not [CollisionConsts] -> false. A repeated "damage" type keeps its first
// value (map insert semantics). Returns true.
bool __cdecl Collision::Load(INI_Reader& Reader)
{
    if (!Reader.is_header("CollisionConsts"))
        return false;

    while (Reader.read_value())
    {
        if (Reader.is_value("damage"))
        {
            // The amount is read before the TypeId test.
            unsigned TypeId = DecodeType(Reader.get_value_string(0));
            float Amount = Reader.get_value_float(1);
            if (TypeId != 0)
            {
                G_CollisionDamage.insert(std::pair<const unsigned, float>(TypeId, Amount));
            }
        }
        else if (Reader.is_value("max_impact_speed"))
        {
            G_CollisionMaxImpactSpeed = Reader.get_value_float(0);
        }
    }
    return true;
}

// FUN_06302aa0. max_impact_speed is not touched.
void __cdecl Collision::Free()
{
    G_CollisionDamage.clear();
}

// FUN_06302b50: a miss gives 0.0f.
float __cdecl Collision::GetImpactDamageFromType(unsigned TypeId)
{
    std::map<unsigned, float>::const_iterator Found = G_CollisionDamage.find(TypeId);
    if (Found != G_CollisionDamage.end())
    {
        return Found->second;
    }
    return 0.0f;
}

// FUN_06302ba0: type damage * (|RelVel| / max_impact_speed) * |Normal . unit(RelVel)|. No
// retail binary calls it; the third argument is the velocity (its length is the one divided by
// max_impact_speed). x87: the length stays unrounded; AbsDot and Ratio are stored as floats.
// Returns 0 for a zero or NaN RelVel length.
float __cdecl Collision::CalcCollisionDamage(unsigned TypeId, Vector const& Normal,
                                              Vector const& RelVel)
{
    const double RelVelX = (double)(RelVel.x);
    const double RelVelY = (double)(RelVel.y);
    const double RelVelZ = (double)(RelVel.z);
    const double Len = sqrt((RelVelX * RelVelX + RelVelY * RelVelY) + RelVelZ * RelVelZ);
    if (!(Len > 0.0))
    {
        return 0.0f;
    }

    const double Inv = 1.0 / Len;
    const double Dot = ((RelVelZ * Inv) * (double)(Normal.z) +
                        (RelVelY * Inv) * (double)(Normal.y)) +
                       (RelVelX * Inv) * (double)(Normal.x);
    const float AbsDot = (float)(fabs(Dot));
    const float Ratio = (float)(Len / (double)(G_CollisionMaxImpactSpeed));

    double Damage = 0.0;
    std::map<unsigned, float>::const_iterator Found = G_CollisionDamage.find(TypeId);
    if (Found != G_CollisionDamage.end())
    {
        Damage = (double)(Found->second);
    }
    return (float)(Damage * ((double)(Ratio) * (double)(AbsDot)));
}

// FUN_06302cb0: COLLISION_DAMAGE_FACTOR * Speed, scaled by OtherMass / Mass only when
// Mass > OtherMass (NaN takes 1.0). Every retail caller passes the relative speed and two
// get_mass results. x87: Scaled stays unrounded.
float __cdecl Collision::CalcCollisionDamage(float Speed, float Mass, float OtherMass)
{
    const double Scaled = (double)(COLLISION_DAMAGE_FACTOR) * (double)(Speed);
    if (Mass > OtherMass)
    {
        return (float)(Scaled * ((double)(OtherMass) / (double)(Mass)));
    }
    return (float)(Scaled * 1.0);
}

// ============================================================================
// CollisionGroupDesc operators
// ============================================================================

// CollisionGroupDesc (common_cobject.h) compares by Id (ushort at +0x00) only; the struct
// definition provides the operators. Retail: FUN_0626c550 / FUN_0626c570 / FUN_0626c590 /
// FUN_0626c5b0 (<, >, ==, !=); operator= is ICF-folded with option_spec::operator=
// (FUN_06280ee0, an 8-byte copy).

// ============================================================================
// PhyArch additions
// ============================================================================

// FUN_062802d0. The default part gets its surface on the first call; the test is the surface
// pointer itself, there is no separate "initialised" flag.
PhyArch::Part const* __cdecl PhyArch::GetDefaultPart()
{
    if (G_DefaultPartStorage.Surface == nullptr)
    {
        IVP_SurfaceBuilder_Ledge_Soup Soup;
        IVP_U_Vector<IVP_U_Float_Point> Points;

        // A pyramid: the four corners of the z = +0.5 face and the apex (0, 0, -0.5). Retail
        // leaves the fourth floats as stack garbage; the port stores 0.
        IVP_U_Float_Point Pyramid[5] = {
            { -0.5f, -0.5f,  0.5f, 0.0f },
            {  0.5f, -0.5f,  0.5f, 0.0f },
            {  0.5f,  0.5f,  0.5f, 0.0f },
            { -0.5f,  0.5f,  0.5f, 0.0f },
            {  0.0f,  0.0f, -0.5f, 0.0f },
        };
        for (int Index = 0; Index < 5; ++Index)
            Points.Add(&Pyramid[Index]);

        // Compiled with options {0, 1, 0, 1}.
        IVP_Compact_Ledge* const Hull = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&Points);
        Soup.InsertLedge(Hull);
        const int32_t CompileOptions[4] = { 0, 1, 0, 1 };
        G_DefaultPartStorage.Surface = (IVP_Compact_Surface*)Soup.Compile(CompileOptions);

        // bNotFixed keeps the constructor's 1; the hardpoint list stays empty.
        G_DefaultPartStorage.ExtentsMin.x = -0.5f;
        G_DefaultPartStorage.ExtentsMin.y = -0.5f;
        G_DefaultPartStorage.ExtentsMin.z = -0.5f;
        G_DefaultPartStorage.ExtentsMax.x = 0.5f;
        G_DefaultPartStorage.ExtentsMax.y = 0.5f;
        G_DefaultPartStorage.ExtentsMax.z = 0.5f;
    }
    return &G_DefaultPartStorage;
}

