// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_deformable.h"

#include "DALib.h"
#include "GameFlow/character_info.h"      // GetCharacterInfoDb
#include "common_goods.h"        // HardpointSummary, HardpointInfo
#include "common_id.h"           // CreateID
#include "common_misc.h"         // malloc_strfree
#include "GameFlow/gf_character_object.h"
#include "GameFlow/Thorn/gf_script_object.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Retail's layout: the index -1 slot accesses below land on the same members as retail's.
static_assert(sizeof(FThornDeformable::FMotionSlot) == 0x10, "Thorn_Deformable motion stride 0x10");
static_assert(sizeof(FThornDeformable::FAimSlot) == 0x3c, "Thorn_Deformable aim stride 0x3c");
static_assert(offsetof(FThornDeformable, bReference) == 0x68, "Thorn_Component +0x68");
static_assert(offsetof(FThornDeformable, Character) == 0x6c, "Thorn_Deformable +0x6c");
static_assert(offsetof(FThornDeformable, FloorHeight) == 0x74, "Thorn_Deformable +0x74");
static_assert(offsetof(FThornDeformable, Motions) == 0x78, "Thorn_Deformable +0x78");
static_assert(offsetof(FThornDeformable, Aims) == 0x278, "Thorn_Deformable +0x278");

namespace
{
    char const* const GScriptDeformableFile =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptDeformable.cpp";

    void* const kDeformNoScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));

    Vector InstancePosition(InstanceIndex Instance)
    {
        return *DALib::Engine->GetInstancePosition(Instance);
    }

    Matrix InstanceOrientation(InstanceIndex Instance)
    {
        return *DALib::Engine->GetInstanceOrientation(Instance);
    }
}

// LAB_00451f50.
FThornDeformable::FMotionSlot::FMotionSlot() : Handle(-1), ScriptId(0), EventId(-1), bActive(0)
{
}

// LAB_00451f70.
FThornDeformable::FAimSlot::FAimSlot() : Handle(nullptr), Key(nullptr), bActive(0), bStarted(0)
{
    Target.Pos.x = 0.0f;
    Target.Pos.y = 0.0f;
    Target.Pos.z = 0.0f;
    MatrixSetIdentity(Target.Orient);
}

// FUN_00451fd0. The two slot arrays are built by FUN_005b7fba (array ctor).
FThornDeformable::FThornDeformable(Csys const& ScriptFrame, FGfScript* InOwner,
                                   FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Character(nullptr),
      ActorName(nullptr),
      FloorHeight(0.0f)
{
}

// FUN_004520c0: the two arrays' element destructors are the empty IDPMsgHandler::OnDisconnect
// fold, then FUN_0044fd80.
FThornDeformable::~FThornDeformable()
{
}

// LAB_00452240. AcquireScript is CharacterInfo FUN_00434d10, AddMotionHandle FUN_004456e0. With a
// locked bone the IAnimation2 slot 36 answer is returned, otherwise IDeformable slot 7's.
// Retail: with all 32 slots busy the index stays -1 and slot 7 still gets &Motions[-1].Handle,
// this + 0x68: the reference byte and its padding.
GENRESULT DACOM_API FThornDeformable::StartMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    char const* const LockedBone = Desc->LockedBone;
    unsigned const ScriptId = CreateID(Desc->Animation);
    int const EventId = Desc->Common.UniqueId;

    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (Motions[Index].bActive != 0)
            continue;

        Slot = Index;
        Motions[Slot].EventId = EventId;
        Motions[Slot].ScriptId = ScriptId;
        Motions[Slot].bActive = 1;
        break;
    }

    void* const AnimScript = GetCharacterInfoDb()->AcquireScript(ScriptId);
    if (AnimScript == kDeformNoScript)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Animation (%s) does not exist, skipping\n",
              GScriptDeformableFile, 0x14b, Desc->Animation);
        return GR_OK;
    }

    GENRESULT const PlayResult = DALib::Deform->Play(
        Character->Lod.Instance, &Motions[Slot].Handle, AnimScript,
        const_cast<char*>(Desc->Animation), AsOpaqueDword(Desc->StartTime),
        AsOpaqueDword(Desc->TransTime), AsOpaqueDword(Desc->TimeScale),
        AsOpaqueDword(Desc->Weight), AsOpaqueDword(Desc->Common.EventFlags),
        AsOpaqueDword(Desc->Heading), nullptr);
    Character->AddMotionHandle(Motions[Slot].Handle);

    if (LockedBone == nullptr)
        return PlayResult;
    long const Part = Character->FindPart(LockedBone);
    if (Part == -1)
        return PlayResult;
    unsigned long const BlockResult =
        DALib::Anim->RegisterHandle(static_cast<unsigned long>(Part));
    if (BlockResult != 0)
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: Failed to block bone %s for animation %s, bone may already "
              "be locked\n",
              GScriptDeformableFile, 0x143, LockedBone, Desc->Animation);
    }
    return static_cast<GENRESULT>(BlockResult);
}

// LAB_004523a0. The character forgets the slot's handle (FUN_004456f0) either way. Always 0.
// Retail: with no matching slot the index stays -1 and Motions[-1] is this + 0x68: Handle the
// reference byte and padding, ScriptId Character, EventId ActorName, bActive FloorHeight's low
// byte; when that byte is set the reset writes null into Character and -1 into ActorName.
GENRESULT DACOM_API FThornDeformable::StopMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (Motions[Index].bActive != 0 && Motions[Index].EventId == Desc->Common.UniqueId)
        {
            Slot = Index;
            break;
        }
    }

    if (Desc->LockedBone != nullptr)
    {
        long const Part = Character->FindPart(Desc->LockedBone);
        if (Part != -1)
            DALib::Anim->HasAnyHandle(static_cast<unsigned long>(Part));
    }

    FMotionSlot& Motion = Motions[Slot];
    Character->RemoveMotionHandle(Motion.Handle);
    if (Motion.bActive != 0)
    {
        DALib::Deform->DestroyHandle(Character->Lod.Instance, Motion.Handle);
        GetCharacterInfoDb()->ReleaseScript(Motion.ScriptId);
        Motion.Handle = -1;
        Motion.ScriptId = 0;
        Motion.EventId = -1;
        Motion.bActive = 0;
    }
    return GR_OK;
}

// LAB_00452d00. The character gets the world y of the script-frame point (0, Height, 0).
GENRESULT DACOM_API FThornDeformable::SetFloorHeight(float Height)
{
    FloorHeight = Height;
    Vector Point;
    Point.x = 0.0f;
    Point.y = Height;
    Point.z = 0.0f;
    Vector const World = Frame.trans_from_frame(Point);
    Character->SetFloorHeight(World.y);
    return GR_OK;
}

// LAB_00452210. 0.001 is the double at 0x005c75f8; the product is passed as a double.
GENRESULT DACOM_API FThornDeformable::AdvanceTime(int DeltaMs)
{
    Character->Update(static_cast<double>(DeltaMs) * 0.001);
    return GR_OK;
}

// FUN_00452d70. The entity order is the Compound's swapped.
GENRESULT DACOM_API FThornDeformable::ConnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    ConnectEntityHardpoints(Desc->Common.SecondaryIndex, Desc->ParentHardpoint,
                            Desc->Common.PrimaryIndex, Desc->Hardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 1);
    return GR_OK;
}

// FUN_00452db0.
GENRESULT DACOM_API FThornDeformable::DisconnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    DisconnectEntityHardpoints(Desc->Common.SecondaryIndex, Desc->ParentHardpoint,
                               Desc->Common.PrimaryIndex, Desc->Hardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 0);
    return GR_OK;
}

// LAB_00452730. Unlike the Compound's, the answer is converted into the script frame; an unknown
// target type answers the origin / identity.
GENRESULT DACOM_API FThornDeformable::GetHardpointXform(int TargetType, char const* Name,
                                                        Transform* Out)
{
    Vector Pos;
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    Matrix Orient;
    MatrixSetIdentity(Orient);

    if (TargetType == 0)
    {
        const InstanceIndex Instance = Character->GetEngineInstance();
        if (Instance == InvalidInstanceIndex)
            return GR_GENERIC;
        Pos = InstancePosition(Instance);
        Orient = InstanceOrientation(Instance);
    }
    else if (TargetType == 2)
    {
        long const Part = Character->FindPart(Name);
        if (Part == -1)
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: Invalid part name %s in get_sub_part_transform()\n",
                  GScriptDeformableFile, 0x255, Name);
            return GR_GENERIC;
        }
        Pos = InstancePosition(Part);
        Orient = InstanceOrientation(Part);
    }
    else if (TargetType == 1)
    {
        HardpointSummary& Hardpoints = Character->Hardpoints;
        int const Index = Hardpoints.find_name(Name);
        if (Index == -1)
        {
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: Invalid hardpoint name %s in get_sub_part_transform()\n",
                  GScriptDeformableFile, 0x265, Name);
            return GR_GENERIC;
        }
        long const HpInstance = Hardpoints.get_instance(Index);
        Vector const HpPos = Hardpoints.get_info(Index).Position;
        Matrix const HpOrient = Hardpoints.get_info(Index).Orient;
        Csys InstanceFrame;
        InstanceFrame.Pos = InstancePosition(HpInstance);
        InstanceFrame.Orient = InstanceOrientation(HpInstance);
        Pos = InstanceFrame.trans_from_frame(HpPos);
        Orient = InstanceFrame.trans_from_frame(HpOrient);
    }

    Out->Translation = Frame.trans_to_frame(Pos);
    Out->Rotation = Frame.trans_to_frame(Orient);
    return GR_OK;
}

// LAB_00452d50.
GENRESULT DACOM_API FThornDeformable::GetFloorHeight(float* OutHeight)
{
    *OutHeight = FloorHeight;
    return GR_OK;
}

// LAB_00452b50. Position first.
GENRESULT DACOM_API FThornDeformable::SetTransform(const Transform* Transform12)
{
    Transform const* In = Transform12;
    Vector WorldPos = Frame.trans_from_frame(In->Translation);
    DALib::Deform->SetPosePos(Character->Lod.Instance, &WorldPos);
    Matrix WorldOrient = Frame.trans_from_frame(In->Rotation);
    DALib::Deform->SetPoseOri(Character->Lod.Instance, &WorldOrient);
    return GR_OK;
}

// LAB_00452c70.
GENRESULT DACOM_API FThornDeformable::GetTransform(Transform* OutTransform12)
{
    const InstanceIndex Instance = Character->GetEngineInstance();
    Transform* Out = OutTransform12;
    Out->Translation = Frame.trans_to_frame(InstancePosition(Instance));
    Out->Rotation = Frame.trans_to_frame(InstanceOrientation(Instance));
    return GR_OK;
}

// LAB_00452460. The root ancestor (FUN_0044fa60) is computed before either part is tested.
// AddAimHandle is FUN_00445700. bActive stays set even when the start fails.
GENRESULT DACOM_API FThornDeformable::StartIk(void* DescArg, Vector* Position, Matrix* Orientation)
{
    FThornStartIkDesc const* Desc = static_cast<FThornStartIkDesc const*>(DescArg);
    long const EndPart = Character->FindPart(Desc->EndEffector);
    const InstanceIndex Root = GfEngineInstanceAncestor(EndPart, Desc->CountToRoot);
    if (EndPart == -1 || Root == InvalidInstanceIndex)
        return GR_GENERIC;

    Vector const TargetPos = Frame.trans_from_frame(*Position);
    Matrix const TargetOrient = Frame.trans_from_frame(*Orientation);

    int Slot = -1;
    int Index = 0;
    for (; Index < kAimSlots; ++Index)
    {
        if (Aims[Index].bActive == 0)
        {
            Slot = Index;
            break;
        }
    }
    if (Slot == -1)
        return GR_GENERIC;

    FAimSlot& Aim = Aims[Slot];
    Aim.Key = Desc->Key;
    Aim.Target.Pos = TargetPos;
    Aim.bActive = 1;
    Aim.Target.Orient = TargetOrient;

    unsigned long const EventFlags = static_cast<unsigned long>(Desc->Common.EventFlags);
    FDeformAimDesc AimDesc;
    AimDesc.Name = "";
    AimDesc.RootPart = Root;
    AimDesc.EndPart = EndPart;
    AimDesc.Flags = EventFlags;
    AimDesc.TargetPos = &Aim.Target.Pos.x;
    AimDesc.TargetOrient = &Aim.Target.Orient.E[0][0];
    AimDesc.Damping = Desc->Damping;
    AimDesc.Field1C = 0;
    if (Desc->bPointAt != 0)
    {
        AimDesc.Flags = EventFlags | kIkAimOrientationTarget;
        if (Desc->CountToRoot == 1)
            AimDesc.Flags = EventFlags | kIkAimOrientationTarget | kIkAimNoClamp;
    }
    if (Desc->bMoveTo != 0)
        AimDesc.Flags |= kIkAimPositionTarget;
    if (static_cast<signed char>(EventFlags & 0xff) < 0)
        AimDesc.Flags |= 0x100;

    GENRESULT const Result = DALib::Deform->StartAim(
        Character->Lod.Instance, &Aim.Handle, reinterpret_cast<unsigned char*>(&AimDesc),
        AsOpaqueDword(Desc->TransitionDuration));
    Character->AddAimHandle(Aim.Handle);
    if (Result == 0)
    {
        Aim.bStarted = 1;
        return GR_OK;
    }
    FDUMP(kFdumpTrace1, "%s(%d) : TRACE_1:General:%s", GScriptDeformableFile, 0x213,
          "Invalid Deform::start_aim() command");
    Aim.bStarted = 0;
    return GR_GENERIC;
}

// FUN_00452a30. RemoveAimHandle is FUN_00445710. Retail: with no matching slot the index stays
// -1 and Aims[-1] is this + 0x23c: Handle Motions[28].ScriptId, bActive Motions[31].bActive,
// bStarted the padding byte after it.
GENRESULT DACOM_API FThornDeformable::StopIk(void* DescArg)
{
    FThornStartIkDesc const* Desc = static_cast<FThornStartIkDesc const*>(DescArg);
    int Slot = -1;
    int Index = 0;
    for (; Index < kAimSlots; ++Index)
    {
        if (Aims[Index].bActive != 0 && Aims[Index].Key == Desc->Key)
        {
            Slot = Index;
            break;
        }
    }

    FAimSlot& Aim = Aims[Slot];
    Character->RemoveAimHandle(Aim.Handle);
    GENRESULT Result = GR_GENERIC;
    if (Aim.bActive != 0)
    {
        if (Aim.bStarted != 0)
            Result = DALib::Deform->StopAim(Character->Lod.Instance, Aim.Handle);
        Aim.bActive = 0;
    }
    return Result;
}

// LAB_00452680. Retail: with no matching slot the index stays -1 and Aims[-1] (this + 0x23c,
// see StopIk) is read and written.
GENRESULT DACOM_API FThornDeformable::UpdateIk(void* DescArg, Vector* Position, Matrix* Orientation)
{
    FThornStartIkDesc const* Desc = static_cast<FThornStartIkDesc const*>(DescArg);
    int Slot = -1;
    int Index = 0;
    for (; Index < kAimSlots; ++Index)
    {
        if (Aims[Index].bActive != 0 && Aims[Index].Key == Desc->Key)
        {
            Slot = Index;
            break;
        }
    }

    FAimSlot& Aim = Aims[Slot];
    if (Aim.bStarted == 0)
        return GR_GENERIC;
    Aim.Target.Pos = Frame.trans_from_frame(*Position);
    Aim.Target.Orient = Frame.trans_from_frame(*Orientation);
    return GR_OK;
}

// LAB_00452ab0.
GENRESULT DACOM_API FThornDeformable::SetPosition(const Vector* Position)
{
    Vector WorldPos = Frame.trans_from_frame(*Position);
    DALib::Deform->SetPosePos(Character->Lod.Instance, &WorldPos);
    return GR_OK;
}

// LAB_00452b00.
GENRESULT DACOM_API FThornDeformable::SetOrientation(const Matrix* Orient9)
{
    Matrix WorldOrient = Frame.trans_from_frame(*Orient9);
    DALib::Deform->SetPoseOri(Character->Lod.Instance, &WorldOrient);
    return GR_OK;
}

// LAB_00452bd0.
GENRESULT DACOM_API FThornDeformable::GetPosition(Vector* OutPosition)
{
    const InstanceIndex Instance = Character->GetEngineInstance();
    *OutPosition = Frame.trans_to_frame(InstancePosition(Instance));
    return GR_OK;
}

// LAB_00452c20.
GENRESULT DACOM_API FThornDeformable::GetOrientation(Matrix* OutOrient9)
{
    const InstanceIndex Instance = Character->GetEngineInstance();
    *OutOrient9 = Frame.trans_to_frame(InstanceOrientation(Instance));
    return GR_OK;
}

// FUN_00452130 (slot 14). bStarted is left as it is. FUN_00445540 (ResetScriptState) is called
// on the character WITHOUT the null check the lines above it make, as retail.
void FThornDeformable::Stop()
{
    malloc_strfree(&ActorName);
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        FMotionSlot& Motion = Motions[Index];
        if (Motion.bActive == 0)
            continue;
        DALib::Deform->DestroyHandle(Character->Lod.Instance, Motion.Handle);
        GetCharacterInfoDb()->ReleaseScript(Motion.ScriptId);
        Motion.Handle = -1;
        Motion.ScriptId = 0;
        Motion.EventId = -1;
        Motion.bActive = 0;
    }
    for (Index = 0; Index < kAimSlots; ++Index)
    {
        FAimSlot& Aim = Aims[Index];
        if (Aim.bActive == 0)
            continue;
        if (Aim.bStarted != 0)
            DALib::Deform->StopAim(Character->Lod.Instance, Aim.Handle);
        Aim.bActive = 0;
    }
    if (Character != nullptr)
    {
        Character->LightGroup = 0;
        Character->Layer = 0;
        Character->RenderMode = 0;
    }
    Character->ResetScriptState(Owner->bOwnScript);
}

// LAB_00452df0. Hands out the character's +0x1b4 (voice id) and the character itself.
bool FThornDeformable::FindActor(char const* InActorName, void** OutActorField, FCharacterObject** OutActor)
{
    if (InActorName == nullptr)
        return false;
    if (_stricmp(ActorName, InActorName) != 0)
        return false;
    *OutActorField = reinterpret_cast<void*>(static_cast<uintptr_t>(Character->Props.VoiceId));
    *OutActor = Character;
    return true;
}

// LAB_00452200 -- tail jump to the character's RoomObject slot 6.
InstanceIndex FThornDeformable::GetEngineInstance()
{
    return Character->GetEngineInstance();
}

// LAB_00452ec0 -- no gate, unlike the Compound's.
void FThornDeformable::SetScene(void* Scene)
{
    Character->AmbientSource = Scene;
}

// LAB_00452ed0 -- no null check.
void FThornDeformable::SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                                unsigned char bRunningLights)
{
    Character->bVisible = bVisible;
    Character->bLit = bLit;
    Character->bFog = bFog;
    Character->bRunningLights = bRunningLights;
}

// FUN_00452e40.
Csys FThornDeformable::GetCharacterCsys() const
{
    const InstanceIndex Instance = Character->GetEngineInstance();
    Csys Out;
    Out.Pos = InstancePosition(Instance);
    Out.Orient = InstanceOrientation(Instance);
    return Out;
}

// FUN_00452eb0.
void* FThornDeformable::GetDeformInstance() const
{
    return Character->Lod.Instance;
}
