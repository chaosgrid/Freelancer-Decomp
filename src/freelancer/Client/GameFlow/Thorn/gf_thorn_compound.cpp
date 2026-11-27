// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_compound.h"

#include "DALib.h"
#include "GameFlow/Thorn/gf_script_object.h"
#include "GameFlow/room_control.h"
#include "x86math.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// The motion arrays sit back to back as in retail: the index -1 accesses of StartMotion /
// StopMotion land on the neighbouring member, as retail's do.
static_assert(offsetof(FThornCompound, Object) == 0x6c, "Thorn_Compound +0x6c");
static_assert(offsetof(FThornCompound, Local) == 0xb0, "Thorn_Compound +0xb0");
static_assert(offsetof(FThornCompound, Playback) == 0xe0, "Thorn_Compound +0xe0");
static_assert(offsetof(FThornCompound, EventId) == 0x120, "Thorn_Compound +0x120");
static_assert(offsetof(FThornCompound, bPlaying) == 0x160, "Thorn_Compound +0x160");
static_assert(offsetof(FThornCompound, Hardpoints) == 0x170, "Thorn_Compound +0x170");

namespace
{
    char const* const GScriptCompoundFile =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptCompound.cpp";

    void* const kCompoundNoAnimScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    unsigned long const kCompoundNoPlayback = 0xFFFFFFFFu;

    // DAT_0066d11c -- this file's 3DMathEngine, acquired (FUN_005b73e0) on first use by each of
    // the eight bodies that read it.
    I3DMathEngine* G_ThornCompoundMath = nullptr;

    I3DMathEngine* ThornCompoundMath()
    {
        if (G_ThornCompoundMath == nullptr)
            G_ThornCompoundMath = Acquire3DMathEngine();
        return G_ThornCompoundMath;
    }

    Vector const& AsMathVector(Vector const& Value)
    {
        return Value;
    }

    Matrix const& AsMathMatrix(Matrix const& Value)
    {
        return Value;
    }

    Matrix const& AsMatrix(Matrix const& Value)
    {
        return Value;
    }

    // The transposed copy LAB_004517b0 / LAB_00451930 / LAB_00451a00 build.
    Matrix Transposed(Matrix const& Source)
    {
        Matrix Out;
        int Row = 0;
        for (; Row < 3; ++Row)
        {
            int Col = 0;
            for (; Col < 3; ++Col)
                Out.E[Row][Col] = Source.E[Col][Row];
        }
        return Out;
    }

    // The script-frame view of the instance, shared by LAB_004517b0 / LAB_00451930 /
    // LAB_00451a00: R = to_frame(instance orientation) * Offset.Orient^T, and the position
    // to_frame(instance position) - R * Offset.Pos.
    void LocalFromInstance(FThornCompound const& Self, Vector& OutPos, Matrix& OutOrient)
    {
        const InstanceIndex Instance = Self.Instance;
        Vector const LocalPos = Self.Frame.trans_to_frame(
            *DALib::Engine->GetInstancePosition(Instance));
        Matrix const LocalOrient = Self.Frame.trans_to_frame(
            *DALib::Engine->GetInstanceOrientation(Instance));
        Matrix const OffsetTransposed = Transposed(Self.Offset.Orient);

        Matrix Product;
        ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(LocalOrient),
                                       AsMathMatrix(OffsetTransposed));
        Vector Rotated;
        ThornCompoundMath()->TransformMatrix(Rotated, Product, AsMathVector(Self.Offset.Pos));

        OutPos.x = LocalPos.x - Rotated.x;
        OutPos.y = LocalPos.y - Rotated.y;
        OutPos.z = LocalPos.z - Rotated.z;
        OutOrient = AsMatrix(Product);
    }

    // Local.Orient * Offset.Pos + Local.Pos (LAB_004514f0 / LAB_00451660).
    Vector InstancePosFromLocal(FThornCompound const& Self)
    {
        Vector Rotated;
        ThornCompoundMath()->TransformMatrix(Rotated, AsMathMatrix(Self.Local.Orient),
                                             AsMathVector(Self.Offset.Pos));
        Vector World;
        World.x = Rotated.x + Self.Local.Pos.x;
        World.y = Rotated.y + Self.Local.Pos.y;
        World.z = Rotated.z + Self.Local.Pos.z;
        return World;
    }

    // Local.Orient * Offset.Orient (LAB_004515b0 / LAB_00451660).
    Matrix InstanceOrientFromLocal(FThornCompound const& Self)
    {
        Matrix Product;
        ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(Self.Local.Orient),
                                       AsMathMatrix(Self.Offset.Orient));
        return AsMatrix(Product);
    }
}

// FUN_004510f0. +0x7c (bUseScene) is not written.
FThornCompound::FThornCompound(Csys const& ScriptFrame, Csys const& InOffset, FGfScript* InOwner,
                               FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Object(nullptr),
      Instance(-1),
      AnimScript(kCompoundNoAnimScript),
      Category(-1),
      Offset(InOffset)
{
    Local.Pos.x = 0.0f;
    Local.Pos.y = 0.0f;
    Local.Pos.z = 0.0f;
    MatrixSetIdentity(Local.Orient);
    int Slot = 0;
    for (; Slot < kMotionSlots; ++Slot)
    {
        Playback[Slot] = kCompoundNoPlayback;
        EventId[Slot] = -1;
        bPlaying[Slot] = 0;
    }
    Hardpoints.clear();
}

// FUN_00451240: ~HardpointSummary, then FUN_0044fd80.
FThornCompound::~FThornCompound()
{
}

// FUN_004513b0. Always 0.
//
// Retail: with all sixteen slots busy the slot index stays -1 and the three stores go to [-1]:
// EventId[-1] is Playback[15] (+0x11c), bPlaying[-1] the top byte of EventId[15] (+0x15f),
// Playback[-1] Local.Orient's last element (+0xdc). The asserts at the top keep the port's
// layout identical, so the same members are hit.
GENRESULT DACOM_API FThornCompound::StartMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    if (AnimScript == kCompoundNoAnimScript)
        return GR_OK;

    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (bPlaying[Index] == 0)
        {
            Slot = Index;
            break;
        }
    }

    EventId[Slot] = Desc->Common.UniqueId;
    bPlaying[Slot] = 1;
    unsigned long const PlaybackId = DALib::Anim->PlayScript(
        AnimScript, Instance, Desc->Animation, nullptr, nullptr);
    Playback[Slot] = PlaybackId;
    if (PlaybackId != kCompoundNoPlayback)
    {
        DALib::Anim->EvalPlayback(PlaybackId, static_cast<unsigned long>(Desc->Common.EventFlags),
                                  Desc->StartTime, Desc->TimeScale,
                                  AsOpaqueDword(Desc->TransTime), AsOpaqueDword(Desc->Weight),
                                  AsOpaqueDword(Desc->TransScale), nullptr);
    }
    return GR_OK;
}

// FUN_00451450. Retail: with no matching slot the index stays -1 and bPlaying[-1], the top byte
// of EventId[15], is tested; when it is set, Playback[-1] (+0xdc) is stopped and the three [-1]
// stores follow. Always 0.
GENRESULT DACOM_API FThornCompound::StopMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    if (AnimScript == kCompoundNoAnimScript)
        return GR_OK;

    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (bPlaying[Index] != 0 && EventId[Index] == Desc->Common.UniqueId)
        {
            Slot = Index;
            break;
        }
    }
    if (bPlaying[Slot] == 0)
        return GR_OK;

    DALib::Anim->StopPlayback(Playback[Slot]);
    DALib::Anim->DestroyScript(reinterpret_cast<void*>(static_cast<uintptr_t>(Playback[Slot])));
    Playback[Slot] = kCompoundNoPlayback;
    EventId[Slot] = -1;
    bPlaying[Slot] = 0;
    return GR_OK;
}

// FUN_00451e50.
GENRESULT DACOM_API FThornCompound::ConnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    ConnectEntityHardpoints(Desc->Common.PrimaryIndex, Desc->Hardpoint,
                            Desc->Common.SecondaryIndex, Desc->ParentHardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 1);
    return GR_OK;
}

// FUN_00451e90.
GENRESULT DACOM_API FThornCompound::DisconnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    DisconnectEntityHardpoints(Desc->Common.PrimaryIndex, Desc->Hardpoint,
                               Desc->Common.SecondaryIndex, Desc->ParentHardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 0);
    return GR_OK;
}

// LAB_00451b90. A world-space answer (no script-frame conversion). The Petal-bug WARNING branch
// is unreachable: it needs an empty name, which the second stricmp has already taken.
GENRESULT DACOM_API FThornCompound::GetHardpointXform(int TargetType, char const* Name,
                                                      Transform* Out)
{
    if (TargetType != 0)
    {
        if (TargetType == 2)
        {
            if (_stricmp(Name, "root") != 0 && _stricmp(Name, "") != 0)
            {
                if (Name == nullptr || Name[0] != '\0')
                {
                    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", GScriptCompoundFile, 0x17d,
                          "Thorn requested a sub_part for which the implementation is not written");
                    return GR_OK;
                }
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", GScriptCompoundFile, 0x179,
                      "Petal bug:  Script wants target_type = PART with target_name = <null>\n");
                return GR_OK;
            }
        }
        else if (TargetType == 1)
        {
            int const Index = Hardpoints.find_name(Name);
            if (Index == -1)
                return GR_GENERIC;
            long const HpInstance = Hardpoints.get_instance(Index);
            HardpointInfo const Info = Hardpoints.get_info(Index);
            Vector const InstPos = *DALib::Engine->GetInstancePosition(HpInstance);
            Matrix const InstOrient = *DALib::Engine->GetInstanceOrientation(HpInstance);

            Vector Rotated;
            ThornCompoundMath()->TransformMatrix(Rotated, AsMathMatrix(InstOrient),
                                                 AsMathVector(Info.Position));
            Vector World;
            World.x = Rotated.x + InstPos.x;
            World.y = Rotated.y + InstPos.y;
            World.z = Rotated.z + InstPos.z;
            Matrix Product;
            ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(InstOrient),
                                           AsMathMatrix(Info.Orient));
            Out->Translation = World;
            Out->Rotation = AsMatrix(Product);
            return GR_OK;
        }
        else
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", GScriptCompoundFile, 0x17d,
                  "Thorn requested a sub_part for which the implementation is not written");
            return GR_OK;
        }
    }

    if (Instance == -1)
        return GR_GENERIC;
    Out->Translation = *DALib::Engine->GetInstancePosition(Instance);
    Out->Rotation = *DALib::Engine->GetInstanceOrientation(Instance);
    return GR_OK;
}

// LAB_00451660. Position first, then orientation, then Engine slot 27.
GENRESULT DACOM_API FThornCompound::SetTransform(const Transform* Transform12)
{
    Transform const* In = Transform12;
    Local.Pos = In->Translation;
    Local.Orient = In->Rotation;
    Vector const World = InstancePosFromLocal(*this);
    Matrix const WorldOrient = InstanceOrientFromLocal(*this);

    const InstanceIndex Inst = Instance;
    Vector EnginePos = Frame.trans_from_frame(World);
    DALib::Engine->SetInstancePosition(Inst, &EnginePos);
    Matrix EngineOrient = Frame.trans_from_frame(WorldOrient);
    DALib::Engine->SetInstanceOrientation(Inst, &EngineOrient);
    DALib::Engine->UpdateInstance(Inst, 0, nullptr);
    return GR_OK;
}

// LAB_00451a00.
GENRESULT DACOM_API FThornCompound::GetTransform(Transform* OutTransform12)
{
    Transform* Out = OutTransform12;
    Vector Pos;
    Matrix Orient;
    LocalFromInstance(*this, Pos, Orient);
    Out->Translation = Pos;
    Out->Rotation = Orient;
    return GR_OK;
}

// LAB_004514f0.
GENRESULT DACOM_API FThornCompound::SetPosition(const Vector* Position)
{
    Local.Pos = *Position;
    Vector const World = InstancePosFromLocal(*this);
    const InstanceIndex Inst = Instance;
    Vector EnginePos = Frame.trans_from_frame(World);
    DALib::Engine->SetInstancePosition(Inst, &EnginePos);
    DALib::Engine->UpdateInstance(Inst, 0, nullptr);
    return GR_OK;
}

// LAB_004515b0.
GENRESULT DACOM_API FThornCompound::SetOrientation(const Matrix* Orient9)
{
    Local.Orient = *Orient9;
    Matrix EngineOrient = Frame.trans_from_frame(InstanceOrientFromLocal(*this));
    const InstanceIndex Inst = Instance;
    DALib::Engine->SetInstanceOrientation(Inst, &EngineOrient);
    DALib::Engine->UpdateInstance(Inst, 0, nullptr);
    return GR_OK;
}

// LAB_004517b0.
GENRESULT DACOM_API FThornCompound::GetPosition(Vector* OutPosition)
{
    Vector Pos;
    Matrix Orient;
    LocalFromInstance(*this, Pos, Orient);
    *OutPosition = Pos;
    return GR_OK;
}

// LAB_00451930. Only the orientation half: no position is read.
GENRESULT DACOM_API FThornCompound::GetOrientation(Matrix* OutOrient9)
{
    const InstanceIndex Inst = Instance;
    Matrix const LocalOrient = Frame.trans_to_frame(
        *DALib::Engine->GetInstanceOrientation(Inst));
    Matrix const OffsetTransposed = Transposed(Offset.Orient);
    Matrix Product;
    ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(LocalOrient),
                                   AsMathMatrix(OffsetTransposed));
    *OutOrient9 = AsMatrix(Product);
    return GR_OK;
}

// FUN_00451290 (slot 14).
void FThornCompound::Stop()
{
    Hardpoints.destroy();
    int Slot = 0;
    for (; Slot < kMotionSlots; ++Slot)
    {
        if (bPlaying[Slot] == 0)
            continue;
        DALib::Anim->StopPlayback(Playback[Slot]);
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(static_cast<uintptr_t>(Playback[Slot])));
        Playback[Slot] = kCompoundNoPlayback;
        EventId[Slot] = -1;
        bPlaying[Slot] = 0;
    }
    ResetMotionSlots();
    if (Object == nullptr)
        return;

    Object->LightGroup = 3;
    Object->Layer = 0;
    Object->RenderMode = 0;
    Object->bRunningLights = 0;
}

// LAB_00451360 -- +0x70. (ShipObject's slot 6, 0x005cbe08, is the same folded body.)
InstanceIndex FThornCompound::GetEngineInstance()
{
    return Instance;
}

// LAB_00451ed0 -- only with a room object and bUseScene.
void FThornCompound::SetScene(void* Scene)
{
    if (Object != nullptr && bUseScene != 0)
        Object->AmbientSource = Scene;
}

// LAB_00451ef0 -- only with a room object.
void FThornCompound::SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                              unsigned char bRunningLights)
{
    if (Object == nullptr)
        return;
    Object->bVisible = bVisible;
    Object->bLit = bLit;
    Object->bFog = bFog;
    Object->bRunningLights = bRunningLights;
}

// FUN_00451330.
void FThornCompound::SetInstance(long InInstance)
{
    Instance = InInstance;
    Hardpoints.destroy();
    Hardpoints.analyze_instance(InInstance);
}

// FUN_00451380.
void FThornCompound::ResetMotionSlots()
{
    int Slot = 0;
    for (; Slot < kMotionSlots; ++Slot)
    {
        Playback[Slot] = kCompoundNoPlayback;
        EventId[Slot] = -1;
        bPlaying[Slot] = 0;
    }
}
