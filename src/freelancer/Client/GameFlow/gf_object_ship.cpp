// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/gf_object_ship.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerEquipDescListRetail (FUN_004c4060) & co.
#include "Common/client_cliobj.h"          // CliObjMaterialPaths, CliEngine*, CliObjRecomputeLodPickExplicit
#include "Common/Graphics/fade_to_black.h"          // SetFadeImmediate (FUN_00412070), SetFadeTarget (FUN_00412050)
#include "Core/game_options.h"           // GetGameOption (FUN_005792b0)
#include "GameFlow/gameflow.h"               // FUN_00558a30 / FUN_00558c00
#include "GameFlow/Thorn/gf_script_object.h"       // G_GfScriptManager (DAT_0066d2d0)
#include "Common/Audio/menu_sound_arch.h"        // LookupClientSoundArch (FUN_0042ae40), PlayClientSoundArch
#include "Common/misc_util.h"              // MiscUtilLoadEngineInstance (FUN_0041c540)
#include "Common/Graphics/vis_effect_arch.h"        // FindEffectArch (FUN_0040f080)

#include "DALib.h"
#include "alchemy.h"
#include "common_archetype.h"
#include "common_cobject.h"   // CompoundInstanceList
#include "common_goods.h"
#include "common_ini.h"       // StringAlloc
#include "common_misc.h"      // malloc_strcpy / malloc_strfree
#include "common_newchar.h"   // RtcSlider
#include "dacom.h"
#include "engine/engbase.h"
#include "soundmanager.h"
#include "x86math.h"

#include <list>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // DAT_00668a9c -- the ObjectShip singleton. Written by RebuildModel, cleared by the destructor.
    FObjectShip* G_GfObjectShip = nullptr;

    // DAT_00668a98 -- ObjectShip.cpp's 3DMathEngine, FUN_005b73e0 on first use.
    I3DMathEngine* G_ObjectShipMath = nullptr;
    // DAT_00668aac -- the second one, FUN_00448c40's.
    I3DMathEngine* G_ObjectShipMountMath = nullptr;

    // .rdata 0x005cbdbc.
    char const kObjectShipCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ObjectShip.cpp";

    I3DMathEngine* ObjectShipMath()
    {
        if (G_ObjectShipMath == nullptr)
            G_ObjectShipMath = Acquire3DMathEngine();
        return G_ObjectShipMath;
    }

    I3DMathEngine* ObjectShipMountMath()
    {
        if (G_ObjectShipMountMath == nullptr)
            G_ObjectShipMountMath = Acquire3DMathEngine();
        return G_ObjectShipMountMath;
    }

    // IFxEffect slots 7 / 8 take the float Delta as a pointer-sized argument.
    void* FloatArg(float Value)
    {
        void* Out = nullptr;
        memcpy(&Out, &Value, sizeof(Value));
        return Out;
    }

    // The inverse frame the ObjectShip functions build inline: FUN_00447be0 (R^T P), FUN_00411350
    // (negated), FUN_00447b60 with the nine elements pushed transposed, FUN_00447bb0.
    void ShipInverseFrame(FTransform& Out, FMatrix const& R, FVector const& P)
    {
        FVector Rotated;
        ShipTransposeRotate(Rotated, R, P);
        FVector Negated;
        Negated.X = -Rotated.X;
        Negated.Y = -Rotated.Y;
        Negated.Z = -Rotated.Z;
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            int Col = 0;
            for (Col = 0; Col < 3; ++Col)
                Out.Rotation.D[Row][Col] = R.D[Col][Row];
        }
        Out.Translation = Negated;
    }

    void ToFMatrix(FMatrix& Out, Matrix const& In)
    {
        memcpy(&Out, &In, sizeof(Out));
    }

    void ToFVector(FVector& Out, Vector const& In)
    {
        Out.X = In.x;
        Out.Y = In.y;
        Out.Z = In.z;
    }

    // FUN_00447ec0 (cdecl Out, Format, ...) -- vsprintf into a 0x100-byte local, then
    // Out->Value = StringAlloc(Buffer, false). The previous value is not freed (retail).
    void FormatHardpointName(CacheString* Out, char const* Format, ...)
    {
        char Buffer[0x100];
        va_list Args;
        va_start(Args, Format);
        vsprintf(Buffer, Format, Args);
        va_end(Args);
        Out->Value = StringAlloc(Buffer, false);
    }

    // FUN_005595a0 (cdecl) -- FUN_00406cd0(Handle, 0, 0) on the companion unless Handle is -1.
    void RecomputeShipCompanionLod(Archetype::ICliObj* Companion, long Handle)
    {
        if (Handle == -1)
            return;
        Archetype::CliObjRecomputeLodPickExplicit(Companion, Handle, nullptr, false);
    }

    // The sound play key FUN_00447f90 builds on the stack (0x18 bytes, FUN_00428680's layout:
    // offset, flags, the position, +0x14).
    struct FEngineSoundKey
    {
        unsigned Offset;    // +0x00 0
        unsigned Flags;     // +0x04 low byte (garbage & 0xf8) | 3: looping, spatial
        float Pos[3];       // +0x08 the hardpoint instance's position
        unsigned Field14;   // +0x14 0
    };
}

// FUN_00447be0.
void ShipTransposeRotate(FVector& Out, FMatrix const& M, FVector const& V)
{
    FVector Scratch;
    ObjectShipMath()->TransposeTransform(Scratch, M, V);
    Out = Scratch;
}

// ---------------------------------------------------------------------------------------
// The description
// ---------------------------------------------------------------------------------------

FObjectShipDesc::FObjectShipDesc()
    : Ship(nullptr), LaunchScript(nullptr), bShadow(0), FloorHeight(0.0f), Equipment(nullptr)
{
    // FUN_00446bf0.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FObjectShipDesc::~FObjectShipDesc()
{
    // FUN_00446c50.
    malloc_strfree(&LaunchScript);
}

// ---------------------------------------------------------------------------------------
// Engine flame effects (+0xa0)
// ---------------------------------------------------------------------------------------

FObjectShipEngineFx::FObjectShipEngineFx()
    : Instance(kRoomObjectNoInstance), bStopping(0), Lifetime(0.0f)
{
    // LAB_00446c60. The frame (+0x0c..+0x3b) is not written.
    Particles.Effect = nullptr;
    Particles.Vis = nullptr;
}

FObjectShipEngineFx::~FObjectShipEngineFx()
{
    // FUN_00446c80.
    Particles.Destroy();
    Instance = kRoomObjectNoInstance;
    bStopping = 0;
    Lifetime = 0.0f;
}

void FObjectShipEngineFx::Start(unsigned ArchId, float InLifetime, void* InInstance,
                                Vector const& InPos, Matrix const& InOrient)
{
    // FUN_00446ca0.
    Particles.Destroy();
    Orient = InOrient;
    Instance = InInstance;
    Pos = InPos;
    bStopping = 0;
    Lifetime = 0.0f;

    // Retail does not test the archetype for null.
    Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
    if (Equip->get_class_type() == Archetype::AClassEngine)
    {
        // The companion's +0x9c flame_effect.
        ID_String const* const Flame = Archetype::CliEngineFlameEffect(Equip->Companion);
        if (!Flame->IsEmpty())
        {
            FEffectArch* const Arch = FindEffectArch(&Flame->Id);
            if (Arch != nullptr)
            {
                if (Particles.Create(Arch->VisEffect))
                {
                    bStopping = 0;
                    Lifetime = InLifetime;
                    Update(0.0f);
                    return;
                }
            }
            else
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: Cannot load engine flame effect ID=%u, name=%s\n",
                      kObjectShipCpp, 0x84, Flame->Id, Flame->get_string());
            }
        }
    }
    Update(0.0f);
}

void FObjectShipEngineFx::Update(float Delta)
{
    // FUN_00446d90.
    if (Particles.Effect == nullptr)
        return;

    // The effect's frame = the instance's transform times the local frame.
    FTransform Local;
    ToFMatrix(Local.Rotation, Orient);
    ToFVector(Local.Translation, Pos);
    FTransform const* const InstanceXform =
        reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(Instance));
    FTransform Product;
    ObjectShipMath()->MulTransform(Product, *InstanceXform, Local);
    FTransform Xform = Product;

    Particles.Effect->SetTransform(nullptr, &Xform);
    Particles.Effect->ComposeTransforms(FloatArg(Delta));
    Particles.Effect->Update(FloatArg(Delta));

    if (bStopping == 0)
    {
        // x87 compare: a negative or NaN Lifetime is endless.
        if (!(static_cast<double>(Lifetime) >= 0.0))
            return;
        // The float is stored, the unrounded difference compared; a NaN keeps counting.
        double const Remaining = static_cast<double>(Lifetime) - static_cast<double>(Delta);
        Lifetime = static_cast<float>(Remaining);
        if (Remaining > 0.0 || Remaining != Remaining)
            return;
        if (Particles.Effect != nullptr)
            Particles.Effect->DestroyNodes(reinterpret_cast<void*>(1));
        bStopping = 1;
        Lifetime = 0.0f;
        return;
    }

    // Stopping: only the low byte of the result is tested.
    GENRESULT const Done = Particles.Effect->IsFinished(reinterpret_cast<void*>(4));
    if ((static_cast<unsigned>(Done) & 0xffu) != 0)
        Particles.Destroy();
}

// ---------------------------------------------------------------------------------------
// Engine sounds (+0xa4 / +0xa8)
// ---------------------------------------------------------------------------------------

FObjectShipEngineSound::FObjectShipEngineSound()
    : Instance(kRoomObjectNoInstance), Source(nullptr)
{
    // LAB_00446eb0.
}

FObjectShipEngineSound::~FObjectShipEngineSound()
{
    // FUN_00446ec0.
    if (Source != nullptr)
        Source->SetLooping(0);
    Instance = kRoomObjectNoInstance;
    Source = nullptr;
}

// ---------------------------------------------------------------------------------------
// ObjectShip
// ---------------------------------------------------------------------------------------

FObjectShip* GfObjectShip()
{
    // FUN_00446be0.
    return G_GfObjectShip;
}

FObjectShip::FObjectShip(FRoomRuntime* InOwner, FObjectShipDesc const& Desc)
    : FRoomObject(InOwner),
      Ship(nullptr),
      LaunchScript(nullptr),
      bShadow(0),
      Equipment(nullptr),
      Instance(kRoomObjectNoInstance),
      EngineFx(nullptr),
      LoopSounds(nullptr),
      RumbleSounds(nullptr),
      ScriptState(0)
{
    // FUN_00446ef0.
    Visuals.ResetFields();
    Load(Desc);
}

FObjectShip::~FObjectShip()
{
    // FUN_00446fb0.
    if (Equipment != nullptr)
        delete Equipment;
    malloc_strfree(&LaunchScript);
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
        Visuals.Detach(Index);
    delete[] EngineFx;
    delete[] LoopSounds;
    delete[] RumbleSounds;
    EngineFx = nullptr;
    LoopSounds = nullptr;
    RumbleSounds = nullptr;
    Visuals.Clear();
    // +0x70 keeps its value after the model is destroyed.
    if (Instance != kRoomObjectNoInstance)
        DALib::Engine->DestroyInstance(Instance);
    // Cleared whichever ship this is.
    G_GfObjectShip = nullptr;
}

bool FObjectShip::IsVisible()
{
    // FUN_0044fd00.
    return false;
}

void FObjectShip::Load(FObjectShipDesc const& Desc)
{
    // FUN_00447120.
    Ship = Desc.Ship;
    Pos = Desc.Pos;
    Orient = Desc.Orient;
    malloc_strcpy(&LaunchScript, Desc.LaunchScript);
    bShadow = Desc.bShadow;
    SetFloorHeight(Desc.FloorHeight);
    Equipment = new EquipDescList();
    Equipment->Items = Desc.Equipment->Items;
    ScriptState = 0;
    RebuildModel();
    RenderMode = 0;
    LightGroup = 3;
    Layer = 0;
    bRunningLights = 0;
}

void FObjectShip::RebuildModel()
{
    // FUN_00447210.
    G_GfObjectShip = this;
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
        Visuals.Detach(Index);
    delete[] EngineFx;
    delete[] LoopSounds;
    delete[] RumbleSounds;
    EngineFx = nullptr;
    LoopSounds = nullptr;
    RumbleSounds = nullptr;
    Visuals.Clear();
    if (Instance != kRoomObjectNoInstance)
    {
        DALib::Engine->DestroyInstance(Instance);
        Instance = kRoomObjectNoInstance;
    }

    // The material entries go into RoomObject +0x20.
    Archetype::ICliObj* const Companion = Ship->Companion;
    long Handle = -1;
    Instance = MiscUtilLoadEngineInstance(Ship->DaArchetype.Value, DALib::Data, nullptr, nullptr,
                                          Archetype::CliObjMaterialPaths(Companion),
                                          &MaterialEntries, &Handle);
    if (Instance != kRoomObjectNoInstance)
    {
        GfShipApplyCollisionDamage(Instance);
        Csys Frame;
        Frame.Pos = Pos;
        Frame.Orient = Orient;
        GfShipPlaceOnMount(Instance, &Frame);
        // +0x13.
        bEnvMap = Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0' ? 1 : 0;
        Visuals.Populate(static_cast<long>(reinterpret_cast<intptr_t>(Instance)));
        if (RtcSlider::is_enabled("ship_loadouts"))
        {
            Visuals.AttachAll(Equipment);
            Visuals.AddFlashLights(Equipment);
        }
        BuildHardpointArrays();
    }
    // On both paths.
    RecomputeShipCompanionLod(Companion, Handle);
}

void FObjectShip::BuildHardpointArrays()
{
    // FUN_00447690.
    int const HpCount = Visuals.Count;
    EngineFx = new FObjectShipEngineFx[HpCount];
    LoopSounds = new FObjectShipEngineSound[HpCount];
    RumbleSounds = new FObjectShipEngineSound[HpCount];
    // get_instance is called once per array.
    int Index = 0;
    for (Index = 0; Index < HpCount; ++Index)
    {
        LoopSounds[Index].Instance =
            reinterpret_cast<void*>(static_cast<intptr_t>(Visuals.Summary->get_instance(Index)));
        RumbleSounds[Index].Instance =
            reinterpret_cast<void*>(static_cast<intptr_t>(Visuals.Summary->get_instance(Index)));
    }
}

void FObjectShip::Update(double DeltaSeconds)
{
    // LAB_004473a0.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;

    float const Delta = static_cast<float>(DeltaSeconds);
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        // A bound sound follows its instance.
        FObjectShipEngineSound& Loop = LoopSounds[Index];
        if (Loop.Instance != kRoomObjectNoInstance && Loop.Source != nullptr)
        {
            float const* const At = DALib::Engine->GetInstancePosition(Loop.Instance);
            float Where[3] = {At[0], At[1], At[2]};
            Loop.Source->SetPosition(Where);
        }
        FObjectShipEngineSound& Rumble = RumbleSounds[Index];
        if (Rumble.Instance != kRoomObjectNoInstance && Rumble.Source != nullptr)
        {
            float const* const At = DALib::Engine->GetInstancePosition(Rumble.Instance);
            float Where[3] = {At[0], At[1], At[2]};
            Rumble.Source->SetPosition(Where);
        }
        EngineFx[Index].Update(Delta);
    }

    DALib::Anim->MixerWalk(static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)), Delta);
    DALib::Engine->UpdateInstance(Instance, 0, AsOpaqueDword(Delta));
    if (bRunningLights != 0)
        Visuals.UpdateFlashLights(Delta);
}

void* FObjectShip::GetEngineInstance()
{
    // LAB_00451360.
    return Instance;
}

void FObjectShip::DrawOpaque(ICamera* Camera)
{
    // LAB_00447500.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;
    if (GetGameOption(kGameOptLikePetal) != 0)
        return;
    if (Instance == kRoomObjectNoInstance)
        return;
    if (Camera == nullptr)
        return;
    if (bShadow != 0)
    {
        float const Height = GetFloorHeight();
        unsigned long HeightBits = 0;
        memcpy(&HeightBits, &Height, sizeof(HeightBits));
        RoomControlDrawShadow(Owner, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)),
                              HeightBits, 0);
    }
    RoomControlDrawObject(Owner, this);
}

void FObjectShip::DrawTranslucent(ICamera* Camera)
{
    // LAB_00447570.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;
    if (bRunningLights == 0)
        return;
    Visuals.DrawFlashLights(Camera);
}

void FObjectShip::DrawEffects(ICamera* Camera)
{
    // LAB_004475b0.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptParticles) == 0)
        return;
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
        EngineFx[Index].Particles.Draw(Camera);
}

void FObjectShip::StartEngineEffects()
{
    // FUN_00447d70. Only the first engine in the player's list is used.
    EquipDescList& List = GetPlayerEquipDescListRetail();
    std::list<EquipDesc>::iterator It = List.Items.begin();
    for (; It != List.Items.end(); ++It)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(It->get_arch_id());
        if (Equip != nullptr && Equip->get_class_type() == Archetype::AClassEngine)
            break;
    }
    if (It == List.Items.end())
        return;

    unsigned const ArchId = It->get_arch_id();
    int const Nozzles = Ship->NumExhaustNozzles; // +0xfc
    CacheString HpName;
    HpName.clear();
    int Number = 0;
    for (Number = 1; Number <= Nozzles; ++Number)
    {
        FormatHardpointName(&HpName, "HpEngine0%d", Number);
        int const Index = Visuals.Summary->find_name(HpName.Value);
        if (Index == -1)
            continue;
        HardpointInfo const& Info = Visuals.Summary->get_info(Index);
        void* const HpInstance =
            reinterpret_cast<void*>(static_cast<intptr_t>(Visuals.Summary->get_instance(Index)));
        EngineFx[Index].Start(ArchId, -1.0f, HpInstance, Info.Position, Info.Orient);
    }
}

void FObjectShip::StopEngineEffects()
{
    // FUN_00447f00.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        FObjectShipEngineFx& Fx = EngineFx[Index];
        if (Fx.Particles.Effect != nullptr)
            Fx.Particles.Effect->DestroyNodes(reinterpret_cast<void*>(1));
        Fx.bStopping = 1;
        Fx.Lifetime = 0.0f;
    }
}

void FObjectShip::StartEngineSound()
{
    // FUN_00447f90. Unlike StartEngineEffects, every engine of the player's list is walked; a
    // later engine overwrites the sources of an earlier one without releasing them (retail).
    EquipDescList& List = GetPlayerEquipDescListRetail();
    std::list<EquipDesc>::iterator It = List.Items.begin();
    for (; It != List.Items.end(); ++It)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(It->get_arch_id());
        if (Equip == nullptr)
            continue;
        if (Equip->get_class_type() != Archetype::AClassEngine)
            continue;

        Archetype::ICliObj* const Companion = Equip->Companion;
        int const Nozzles = Ship->NumExhaustNozzles;
        CacheString HpName;
        HpName.clear();
        int Number = 0;
        for (Number = 1; Number <= Nozzles; ++Number)
        {
            FormatHardpointName(&HpName, "HpEngine0%d", Number);
            int const Index = Visuals.Summary->find_name(HpName.Value);
            if (Index == -1)
                continue;

            // character_loop_sound (+0x78), the element's source as the handle. Retail keeps
            // bits 3..7 of the flag byte from an unwritten stack slot; the port writes 0.
            FObjectShipEngineSound& Loop = LoopSounds[Index];
            float const* const LoopAt = DALib::Engine->GetInstancePosition(Loop.Instance);
            FEngineSoundKey LoopKey;
            LoopKey.Offset = 0;
            LoopKey.Flags = 3;
            LoopKey.Pos[0] = LoopAt[0];
            LoopKey.Pos[1] = LoopAt[1];
            LoopKey.Pos[2] = LoopAt[2];
            LoopKey.Field14 = 0;
            PlayClientSoundArch(LookupClientSoundArch(Archetype::CliEngineCharacterLoopSound(Companion)),
                                reinterpret_cast<unsigned*>(&LoopKey),
                                reinterpret_cast<void**>(&Loop.Source), 0);

            // rumble_sound (+0x60), the same way.
            FObjectShipEngineSound& Rumble = RumbleSounds[Index];
            float const* const RumbleAt = DALib::Engine->GetInstancePosition(Rumble.Instance);
            FEngineSoundKey RumbleKey;
            RumbleKey.Offset = 0;
            RumbleKey.Flags = 3;
            RumbleKey.Pos[0] = RumbleAt[0];
            RumbleKey.Pos[1] = RumbleAt[1];
            RumbleKey.Pos[2] = RumbleAt[2];
            RumbleKey.Field14 = 0;
            PlayClientSoundArch(LookupClientSoundArch(Archetype::CliEngineRumbleSound(Companion)),
                                reinterpret_cast<unsigned*>(&RumbleKey),
                                reinterpret_cast<void**>(&Rumble.Source), 0);
        }
    }
}

void FObjectShip::StopEngineSound()
{
    // FUN_004481b0.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        if (LoopSounds[Index].Source != nullptr)
            LoopSounds[Index].Source->SetLooping(0);
        if (RumbleSounds[Index].Source != nullptr)
            RumbleSounds[Index].Source->SetLooping(0);
    }
}

void FObjectShip::SetEngineSoundAttenuation(float Attenuation)
{
    // FUN_00448210.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        if (LoopSounds[Index].Source != nullptr)
            LoopSounds[Index].Source->SetAttenuation(Attenuation);
        if (RumbleSounds[Index].Source != nullptr)
            RumbleSounds[Index].Source->SetAttenuation(Attenuation);
    }
}

void FObjectShip::SetEquipment(EquipDescList const* List)
{
    // FUN_00448540.
    if (Equipment != nullptr)
        delete Equipment;
    Equipment = new EquipDescList();
    Equipment->Items = List->Items;
}

void FObjectShip::SetShipArchetype(unsigned ArchId)
{
    // FUN_00448680.
    Ship = Archetype::GetShip(ArchId);
}

void FObjectShip::MountEquipment(int bCombined, unsigned ArchId, unsigned short /*Id*/,
                                 CacheString const* Hardpoint, int /*Count*/, float /*Health*/)
{
    // FUN_004477d0. The cargo bay is compared by pointer.
    if (Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
        return;
    int const Index = Visuals.Summary->find_name(Hardpoint->Value);
    if (Index == -1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing hardpoint (%s)", kObjectShipCpp, 0x275,
              Hardpoint->Value);
        return;
    }
    // Only an item that was not folded into an existing one.
    if (bCombined == 0)
        Visuals.Attach(Index, Archetype::GetEquipment(ArchId));
}

void FObjectShip::UnmountEquipment(int RemoveResult, unsigned /*ArchId*/, unsigned /*Id*/,
                                   CacheString const* Hardpoint, unsigned /*Count*/)
{
    // FUN_00447850.
    if (Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
        return;
    int const Index = Visuals.Summary->find_name(Hardpoint->Value);
    if (Index == -1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing hardpoint (%s)", kObjectShipCpp, 0x295,
              Hardpoint->Value);
        return;
    }
    // Only when the item was erased.
    if (RemoveResult == 0)
        Visuals.Detach(Index);
}

void FObjectShip::PlayLaunchScript()
{
    // FUN_00447cd0.
    if (LaunchScript == nullptr)
    {
        RequestGameflowMissionBriefing();
        return;
    }
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Owner, 0);
    Csys Frame;
    Frame.set_identity();
    Sequence->AddScript(LaunchScript, 0, &Frame, nullptr);
    Sequence->BindMode = 0;
    Sequence->bLetterbox = 1;
    Sequence->bFade = 1;
    Sequence->bApplySpatial = 1;
    Sequence->bRestoreCamera = 0;
    Sequence->Priority = 1;
    Sequence->Field_0x16 = 1;
    Sequence->Callback = &FObjectShip::OnScriptStopped;
    Sequence->CallbackA = this;
    Sequence->CallbackB = nullptr;
    Sequence->Play();
    ScriptState = 1;
    RequestGameflowStateFive();
}

void FObjectShip::PlayLandingScript(char const* Script)
{
    // FUN_00447c30.
    SetFadeImmediate(1.0f);
    // 1.0 / DAT_0061650c divided in double, then rounded to float.
    SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Owner, 0);
    Csys Frame;
    Frame.set_identity();
    Sequence->AddScript(Script, 0, &Frame, nullptr);
    Sequence->BindMode = 0;
    Sequence->bLetterbox = 1;
    Sequence->bApplySpatial = 1;
    Sequence->bRestoreCamera = 1;
    Sequence->Priority = 1;
    Sequence->Callback = &FObjectShip::OnScriptStopped;
    Sequence->CallbackA = this;
    Sequence->CallbackB = nullptr;
    Sequence->bPropsToRoom = 1;
    Sequence->Play();
    ScriptState = 2;
}

void __cdecl FObjectShip::OnScriptStopped(void* ShipPtr, void* /*Unused*/, unsigned char /*bArg*/)
{
    // LAB_00448270.
    FObjectShip* const Ship = static_cast<FObjectShip*>(ShipPtr);
    if (Ship->ScriptState == 0)
        return;
    if (Ship->ScriptState == 1)
    {
        // +0xac is left at 1.
        RequestGameflowMissionBriefing();
        return;
    }
    if (Ship->ScriptState == 2)
        Ship->ScriptState = 0;
}

// ---------------------------------------------------------------------------------------
// DealerShipObject
// ---------------------------------------------------------------------------------------

FDealerShipDesc::FDealerShipDesc()
    : Ship(nullptr), bShadow(0), Height(0.0f), Loadout(nullptr)
{
    // FUN_00443770.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FDealerShipObject::FDealerShipObject(FRoomRuntime* InOwner, FDealerShipDesc const& Desc)
    : FRoomObject(InOwner), Instance(kRoomObjectNoInstance), bShadow(0)
{
    // FUN_004437d0.
    Visuals.ResetFields();
    Load(Desc);
}

FDealerShipObject::~FDealerShipObject()
{
    // FUN_004438a0.
    Visuals.Clear();
    if (Instance != kRoomObjectNoInstance)
        DALib::Engine->DestroyInstance(Instance);
}

bool FDealerShipObject::IsVisible()
{
    // FUN_0044fd00.
    return false;
}

void FDealerShipObject::Load(FDealerShipDesc const& Desc)
{
    // FUN_00443920.
    bShadow = Desc.bShadow;
    // 1.6f is the float DAT_005cb908.
    SetFloorHeight(static_cast<float>(static_cast<double>(Desc.Height) - static_cast<double>(1.6f)));

    // The material entries go into RoomObject +0x20.
    Archetype::ICliObj* const Companion = Desc.Ship->Companion;
    long Handle = -1;
    Instance = MiscUtilLoadEngineInstance(Desc.Ship->DaArchetype.Value, DALib::Data, nullptr,
                                          nullptr, Archetype::CliObjMaterialPaths(Companion),
                                          &MaterialEntries, &Handle);
    if (Instance != kRoomObjectNoInstance)
    {
        Csys Frame;
        Frame.Pos = Desc.Pos;
        Frame.Orient = Desc.Orient;
        GfShipPlaceOnMount(Instance, &Frame);
        bEnvMap = Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0' ? 1 : 0;
        // No "ship_loadouts" test and no flashlights here, unlike the player's ship.
        Visuals.Populate(static_cast<long>(reinterpret_cast<intptr_t>(Instance)));
        Visuals.AttachAll(Desc.Loadout);
    }
    // On both paths.
    LightGroup = 3;
    Layer = 0;
    RenderMode = 0;
    RecomputeShipCompanionLod(Companion, Handle);
}

void FDealerShipObject::Update(double DeltaSeconds)
{
    // LAB_00443a10. No option test.
    DALib::Engine->UpdateInstance(Instance, 0, AsOpaqueDword(static_cast<float>(DeltaSeconds)));
}

void* FDealerShipObject::GetEngineInstance()
{
    // LAB_00443870.
    return Instance;
}

void FDealerShipObject::DrawOpaque(ICamera* /*Camera*/)
{
    // FUN_00443a30. No option, model or camera test.
    if (bShadow != 0)
    {
        float const Height = GetFloorHeight();
        unsigned long HeightBits = 0;
        memcpy(&HeightBits, &Height, sizeof(HeightBits));
        RoomControlDrawShadow(Owner, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)),
                              HeightBits, 0);
    }
    RoomControlDrawObject(Owner, this);
}

// ---------------------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------------------

void GfShipApplyCollisionDamage(void* InstancePtr)
{
    // FUN_004482b0. Always the player's ship archetype and collision groups.
    long const Instance = static_cast<long>(reinterpret_cast<intptr_t>(InstancePtr));
    Archetype::Ship* const Ship = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
    std::list<CollisionGroupDesc>& Groups = GetPlayerCollisionGroupListRetail();
    int PartCount = 0;
    long* Parts = nullptr;
    CompoundInstanceList(Instance, &PartCount, &Parts);

    // A destroyed group (health exactly 0; a NaN does not count) removes every part whose
    // engine name is the group's obj.
    std::list<CollisionGroupDesc>::iterator It = Groups.begin();
    for (; It != Groups.end(); ++It)
    {
        if (!(It->Health == 0.0f))
            continue;
        int PartIndex = 0;
        for (PartIndex = 0; PartIndex < PartCount; ++PartIndex)
        {
            void* const Part = reinterpret_cast<void*>(static_cast<intptr_t>(Parts[PartIndex]));
            FEngineName NameOut;
            char const* Name = DALib::Engine->GetInstanceName(&NameOut, Part)->Text;
            if (Name == nullptr)
                Name = "";
            Archetype::CollisionGroup const* const Group = Ship->get_group_by_id(It->Id);
            // Retail re-tests the name after forcing it to "".
            if (Name == nullptr || Group == nullptr)
                continue;
            if (_stricmp(Name, Group->ObjName) != 0) // DACOM stricmp
                continue;
            DALib::Engine->DestroyInstance(Part);
            Parts[PartIndex] = -1;
        }
    }

    HardpointSummary Summary;
    Summary.analyze_instance(Instance);
    // Each destroyed group's dmg_obj meshes hang by "DpConnect" on the dmg_hp hardpoint; the
    // parent instance is not checked (retail).
    for (It = Groups.begin(); It != Groups.end(); ++It)
    {
        if (!(It->Health == 0.0f))
            continue;
        // Retail: no null test on the group here, unlike the loop above.
        Archetype::CollisionGroup const* const Group = Ship->get_group_by_id(It->Id);
        std::vector<Archetype::DamageObjInfo>::const_iterator Dmg = Group->DmgObjs.Items->begin();
        for (; Dmg != Group->DmgObjs.Items->end(); ++Dmg)
        {
            char const* const HpName = Dmg->Hardpoint.Value;
            Archetype::Root* const Arch = Archetype::GetSimple(Dmg->Arch.Id);
            void* const Child = MiscUtilLoadEngineInstance(
                Arch->DaArchetype.Value, DALib::Data, nullptr, nullptr,
                Archetype::CliObjMaterialPaths(Arch->Companion), nullptr, nullptr);
            FHardpointKey ChildKey;
            ChildKey.Name = "DpConnect";
            ChildKey.Crc = DACOM_CRC::GetCRC32("DpConnect");
            FHardpointKey ParentKey;
            ParentKey.Name = HpName;
            ParentKey.Crc = DACOM_CRC::GetCRC32(HpName);
            long const Parent = Summary.find_name_instance(HpName);
            DALib::Hardpoint->ConnectHardpoints(reinterpret_cast<void*>(static_cast<intptr_t>(Parent)),
                                                &ParentKey, Child, &ChildKey);
        }
    }

    Summary.destroy();
    ::operator delete(Parts);
}

void GfShipPlaceOnMount(void* Instance, Csys const* Frame)
{
    // FUN_00448c40. Only the low byte of GetHardpoint's result is tested.
    void* const ArchHandle = DALib::Engine->GetInstanceArchetype(Instance);
    FHardpointKey Key;
    Key.Name = "HpMount";
    Key.Crc = DACOM_CRC::GetCRC32("HpMount");
    HardpointInfo Mount;
    if ((DALib::Hardpoint->GetHardpoint(ArchHandle, &Key, &Mount) & 0xffu) == 0)
    {
        Mount.Position.x = 0.0f;
        Mount.Position.y = 0.0f;
        Mount.Position.z = 0.0f;
        MatrixSetIdentity(Mount.Orient);
    }
    DALib::Engine->ReleaseHandle(ArchHandle);

    FMatrix MountRotation;
    ToFMatrix(MountRotation, Mount.Orient);
    FVector MountPos;
    ToFVector(MountPos, Mount.Position);
    FTransform Inverse;
    ShipInverseFrame(Inverse, MountRotation, MountPos);

    // DAT_00668aac, this function's own math engine.
    FTransform FrameXform;
    ToFMatrix(FrameXform.Rotation, Frame->Orient);
    ToFVector(FrameXform.Translation, Frame->Pos);
    FTransform Placed;
    ObjectShipMountMath()->MulTransform(Placed, FrameXform, Inverse);

    FTransform Copy = Placed;
    DALib::Engine->SetInstanceMatrix(Instance, &Copy);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

Csys GfShipMountOffset(HardpointSummary const* Summary, char const* HpName, void* RootInstance)
{
    // FUN_004478c0.
    Csys Out;
    int const Index = Summary->find_name(HpName);
    if (Index == -1)
    {
        Out.Pos.x = 0.0f;
        Out.Pos.y = 0.0f;
        Out.Pos.z = 0.0f;
        MatrixSetIdentity(Out.Orient);
        return Out;
    }

    // The hardpoint record is 0x48 bytes.
    void* const HpInstance =
        reinterpret_cast<void*>(static_cast<intptr_t>(Summary->get_instance(Index)));
    HardpointInfo const Info = Summary->get_info(Index);
    FMatrix LocalRotation;
    ToFMatrix(LocalRotation, Info.Orient);
    FVector LocalPos;
    ToFVector(LocalPos, Info.Position);

    FTransform const* const HpXform =
        reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(HpInstance));
    FTransform const* const RootXform =
        reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(RootInstance));

    FTransform HpInverse;
    ShipInverseFrame(HpInverse, HpXform->Rotation, HpXform->Translation);
    FTransform Relative;
    ObjectShipMath()->MulTransform(Relative, HpInverse, *RootXform);
    FTransform RelativeCopy = Relative;

    FTransform LocalInverse;
    ShipInverseFrame(LocalInverse, LocalRotation, LocalPos);
    FTransform Result;
    ObjectShipMath()->MulTransform(Result, LocalInverse, RelativeCopy);

    Out.Pos.x = Result.Translation.X;
    Out.Pos.y = Result.Translation.Y;
    Out.Pos.z = Result.Translation.Z;
    memcpy(&Out.Orient, &Result.Rotation, sizeof(Out.Orient));
    return Out;
}
